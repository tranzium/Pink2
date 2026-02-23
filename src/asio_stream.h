// Copyright (c) 2026 The Pinkcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// std::streambuf adapter for standalone Asio SSL/TCP streams.
// Replaces boost::iostreams::stream<SSLIOStreamDevice<Protocol>>.

#ifndef ASIO_STREAM_H
#define ASIO_STREAM_H

#include <asio.hpp>
#include <asio/ssl.hpp>
#include <streambuf>
#include <iostream>
#include <array>
#include <string>

/**
 * AsioSSLStreamBuf -- a std::streambuf that reads/writes through an
 * asio::ssl::stream (or its raw TCP next_layer() when SSL is disabled).
 *
 * Provides the same functionality as the old boost::iostreams SSLIOStreamDevice:
 *   - underflow()  -- refills the get-area from the socket (read)
 *   - overflow()   -- flushes one character to the socket (write)
 *   - xsputn()     -- bulk write to the socket
 *   - sync()       -- flushes the put-area
 *   - connect()    -- resolve + connect to a remote host
 *   - handshake()  -- lazy SSL handshake
 *   - close()      -- shut down the lowest-layer socket
 */
template <typename Protocol>
class AsioSSLStreamBuf : public std::streambuf {
public:
    AsioSSLStreamBuf(asio::ssl::stream<typename Protocol::socket>& streamIn,
                     bool fUseSSLIn)
        : m_stream(streamIn), m_fUseSSL(fUseSSLIn), m_fNeedHandshake(fUseSSLIn)
    {
        // Initialize get-area as empty (will be filled on first underflow)
        setg(m_inBuf.data(), m_inBuf.data(), m_inBuf.data());
    }

    // ---- client-side helpers (mirror old SSLIOStreamDevice API) ----

    /** Resolve and connect to server:port via the underlying socket. */
    bool connect(const std::string& server, const std::string& port)
    {
        asio::ip::tcp::resolver resolver(
            static_cast<asio::io_context&>(
                m_stream.get_executor().context()));
        auto results = resolver.resolve(server, port);
        asio::error_code error = asio::error::host_not_found;
        for (const auto& endpoint : results)
        {
            m_stream.lowest_layer().close();
            m_stream.lowest_layer().connect(endpoint, error);
            if (!error)
                break;
        }
        return !error;
    }

    /** Perform SSL handshake (lazy -- only on first call). */
    void handshake(asio::ssl::stream_base::handshake_type role)
    {
        if (!m_fNeedHandshake) return;
        m_fNeedHandshake = false;
        m_stream.handshake(role);
    }

    /** Shut down the lowest-layer socket. */
    void close()
    {
        asio::error_code ec;
        m_stream.lowest_layer().close(ec);
    }

protected:
    // ---- std::streambuf overrides ----

    /** Called when the get-area is exhausted; reads more data from the socket. */
    int_type underflow() override
    {
        // Trigger server-side handshake on first read
        handshake(asio::ssl::stream_base::server);

        asio::error_code ec;
        std::size_t n = 0;
        if (m_fUseSSL)
            n = m_stream.read_some(asio::buffer(m_inBuf.data(), m_inBuf.size()), ec);
        else
            n = m_stream.next_layer().read_some(asio::buffer(m_inBuf.data(), m_inBuf.size()), ec);

        if (ec || n == 0)
            return traits_type::eof();

        setg(m_inBuf.data(), m_inBuf.data(), m_inBuf.data() + n);
        return traits_type::to_int_type(*gptr());
    }

    /** Called when a single character is written and the put-area is full. */
    int_type overflow(int_type ch) override
    {
        if (ch == traits_type::eof())
            return traits_type::eof();

        char c = traits_type::to_char_type(ch);
        // Trigger client-side handshake on first write
        handshake(asio::ssl::stream_base::client);

        asio::error_code ec;
        if (m_fUseSSL)
            asio::write(m_stream, asio::buffer(&c, 1), ec);
        else
            asio::write(m_stream.next_layer(), asio::buffer(&c, 1), ec);

        return ec ? traits_type::eof() : ch;
    }

    /** Bulk write -- more efficient than one-char-at-a-time overflow. */
    std::streamsize xsputn(const char* s, std::streamsize n) override
    {
        if (n <= 0) return 0;

        // Trigger client-side handshake on first write
        handshake(asio::ssl::stream_base::client);

        asio::error_code ec;
        std::size_t written = 0;
        if (m_fUseSSL)
            written = asio::write(m_stream, asio::buffer(s, static_cast<std::size_t>(n)), ec);
        else
            written = asio::write(m_stream.next_layer(), asio::buffer(s, static_cast<std::size_t>(n)), ec);

        return ec ? 0 : static_cast<std::streamsize>(written);
    }

    /** Flush -- nothing buffered on the put side, so this is a no-op. */
    int sync() override
    {
        return 0;
    }

private:
    asio::ssl::stream<typename Protocol::socket>& m_stream;
    bool m_fUseSSL;
    bool m_fNeedHandshake;

    // Read buffer (4 KB is a reasonable default for HTTP/JSON-RPC traffic)
    static constexpr std::size_t kBufSize = 4096;
    std::array<char, kBufSize> m_inBuf;
};

#endif // ASIO_STREAM_H
