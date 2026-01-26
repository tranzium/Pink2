#!/bin/bash
# Build BerkeleyDB 4.8.30 for MXE cross-compilation
# This ensures wallet.dat backwards compatibility
#
# Usage: ./build-bdb48.sh
#
# Environment variables:
#   MXE_ROOT       - MXE installation path (default: /usr/lib/mxe)
#   INSTALL_PREFIX - Installation path (default: /opt/mxe-bdb48)
#   BUILD_DIR      - Build directory (default: /tmp/bdb48-build)

set -e

# Configuration
BDB_VERSION="4.8.30"
BDB_ARCHIVE="db-${BDB_VERSION}.NC.tar.gz"
BDB_URL="https://download.oracle.com/berkeley-db/${BDB_ARCHIVE}"
BDB_SHA256="12edc0df75bf9abd7f82f821795bcee50f42cb2e5f76a6a281b85732798364ef"

# MXE configuration
MXE_ROOT="${MXE_ROOT:-/usr/lib/mxe}"
MXE_TARGET="x86_64-w64-mingw32.static"
MXE_BIN="${MXE_ROOT}/usr/bin"

# Install prefix - separate from MXE's own BDB 6.x
INSTALL_PREFIX="${INSTALL_PREFIX:-/opt/mxe-bdb48}"

# Working directory
BUILD_DIR="${BUILD_DIR:-/tmp/bdb48-build}"

echo "=== Building BerkeleyDB ${BDB_VERSION} for MXE ==="
echo "MXE Root: ${MXE_ROOT}"
echo "MXE Target: ${MXE_TARGET}"
echo "Install Prefix: ${INSTALL_PREFIX}"
echo ""

# Check MXE toolchain
if [ ! -x "${MXE_BIN}/${MXE_TARGET}-gcc" ]; then
    echo "ERROR: MXE toolchain not found at ${MXE_BIN}"
    echo "Make sure MXE is installed with the mingw-w64 target."
    exit 1
fi

# Create build directory
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# Download if not exists
if [ ! -f "${BDB_ARCHIVE}" ]; then
    echo "Downloading BerkeleyDB ${BDB_VERSION}..."
    wget -q --show-progress "${BDB_URL}" -O "${BDB_ARCHIVE}"
fi

# Verify checksum
echo "Verifying checksum..."
echo "${BDB_SHA256}  ${BDB_ARCHIVE}" | sha256sum -c -
if [ $? -ne 0 ]; then
    echo "ERROR: Checksum verification failed!"
    exit 1
fi

# Extract (clean first)
echo "Extracting..."
rm -rf "db-${BDB_VERSION}.NC"
tar -xzf "${BDB_ARCHIVE}"

cd "db-${BDB_VERSION}.NC"

# Apply MinGW compatibility patches
# These fix conflicts with C++11 atomic_init and __atomic_compare_exchange
echo "Applying MinGW/C++11 compatibility patches..."

# Patch atomic.h - rename conflicting symbols
cat > mingw-atomic.patch << 'EOF'
--- a/dbinc/atomic.h
+++ b/dbinc/atomic.h
@@ -70,7 +70,7 @@ typedef struct {
  * These have no memory barriers; the caller must include them when necessary.
  */
 #define	atomic_read(p)		((p)->value)
-#define	atomic_init(p, val)	((p)->value = (val))
+#define	db_atomic_init(p, val)	((p)->value = (val))

 #ifdef HAVE_ATOMIC_SUPPORT

@@ -144,7 +144,7 @@ typedef LONG volatile *interlocked_val;
 #define	atomic_inc(env, p)	__atomic_inc(p)
 #define	atomic_dec(env, p)	__atomic_dec(p)
 #define	atomic_compare_exchange(env, p, o, n)	\
-	__atomic_compare_exchange((p), (o), (n))
+	__atomic_compare_exchange_db((p), (o), (n))
 static inline int __atomic_inc(db_atomic_t *p)
 {
 	int	temp;
@@ -176,7 +176,7 @@ static inline int __atomic_dec(db_atomic_t *p)
  * http://gcc.gnu.org/onlinedocs/gcc-4.1.0/gcc/Atomic-Builtins.html
  * which differ centered centered am.
  */
-static inline int __atomic_compare_exchange(
+static inline int __atomic_compare_exchange_db(
 	db_atomic_t *p, atomic_value_t oldval, atomic_value_t newval)
 {
 	return __sync_bool_compare_and_swap(&p->value, oldval, newval);
EOF

patch -p1 < mingw-atomic.patch

# Fix all source files that use atomic_init -> db_atomic_init
echo "Updating atomic_init references in source files..."
sed -i 's/atomic_init/db_atomic_init/g' \
    mp/mp_fget.c \
    mp/mp_mvcc.c \
    mp/mp_region.c \
    mutex/mut_method.c \
    mutex/mut_tas.c

# Enter build directory
cd build_unix

# Set up cross-compilation environment
export CC="${MXE_BIN}/${MXE_TARGET}-gcc"
export CXX="${MXE_BIN}/${MXE_TARGET}-g++"
export AR="${MXE_BIN}/${MXE_TARGET}-ar"
export RANLIB="${MXE_BIN}/${MXE_TARGET}-ranlib"
export STRIP="${MXE_BIN}/${MXE_TARGET}-strip"

echo ""
echo "Configuring BerkeleyDB for MinGW cross-compilation..."
../dist/configure \
    --prefix="${INSTALL_PREFIX}" \
    --host=x86_64-w64-mingw32 \
    --enable-cxx \
    --enable-mingw \
    --disable-shared \
    --disable-replication \
    --disable-tcl \
    --disable-java \
    --with-mutex=win32/gcc

echo ""
echo "Building BerkeleyDB (this may take a few minutes)..."
make -j$(nproc)

echo ""
echo "Installing BerkeleyDB to ${INSTALL_PREFIX}..."
echo "NOTE: This requires sudo access."
sudo mkdir -p "${INSTALL_PREFIX}"
sudo make install

# Verify installation
echo ""
echo "=== Verifying Installation ==="
echo "Libraries:"
ls -la "${INSTALL_PREFIX}/lib/"*.a
echo ""
echo "Headers:"
ls "${INSTALL_PREFIX}/include/"*.h | head -5

# Check version in header
echo ""
echo "Installed BerkeleyDB version:"
grep "DB_VERSION" "${INSTALL_PREFIX}/include/db.h" | head -3

echo ""
echo "=== BerkeleyDB ${BDB_VERSION} build complete ==="
echo ""
echo "Install location: ${INSTALL_PREFIX}"
echo ""
echo "The MXE toolchain is already configured to use this path."
echo "Build Pinkcoin with:"
echo "  cmake --preset windows-mxe-daemon"
echo "  cmake --build build/windows-mxe-daemon"
