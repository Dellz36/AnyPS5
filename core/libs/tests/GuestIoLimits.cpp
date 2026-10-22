#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libkernel/File/include/TransferLimits.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

struct GuestIovec {
    void* base;
    std::size_t length;
};

extern "C" {
std::int64_t APS5_VABI sceKernelReadv(int, const GuestIovec*, int);
std::int64_t APS5_VABI sceKernelWritev(int, const GuestIovec*, int);
std::int64_t APS5_VABI sceKernelPreadv(int, const GuestIovec*, int, std::int64_t);
std::int64_t APS5_VABI sceKernelPwritev(int, const GuestIovec*, int, std::int64_t);
std::int64_t APS5_VABI readv_nid_postfix(int, const GuestIovec*, int);
std::int64_t APS5_VABI writev_nid_postfix(int, const GuestIovec*, int);
std::int64_t APS5_VABI preadv_nid_postfix(int, const GuestIovec*, int, std::int64_t);
std::int64_t APS5_VABI pwritev_nid_postfix(int, const GuestIovec*, int, std::int64_t);
int APS5_VABI sceKernelOpen(const char*, int, std::uint16_t);
int APS5_VABI sceKernelClose(int);
std::int64_t APS5_VABI sceKernelRead(int, void*, std::size_t);
std::int64_t APS5_VABI sceKernelWrite(int, const void*, std::size_t);
std::int64_t APS5_VABI sceKernelPread(int, void*, std::size_t, std::int64_t);
std::int64_t APS5_VABI sceKernelPwrite(int, const void*, std::size_t, std::int64_t);
std::int64_t APS5_VABI pread_nid_postfix(int, void*, std::size_t, std::int64_t);
std::int64_t APS5_VABI pwrite_nid_disambig1_nid_postfix(int, const void*, std::size_t, std::int64_t);
std::int64_t APS5_VABI read_nid_postfix(int, void*, std::uint64_t);
std::int64_t APS5_VABI _read_nid_postfix(int, void*, std::size_t);
std::int64_t APS5_VABI _write_nid_postfix(int, const void*, std::size_t);
int* APS5_VABI __error_nid_postfix();
}

namespace {

constexpr int invalid = static_cast<int>(0x80020016);
constexpr int badDescriptor = static_cast<int>(0x80020009);
constexpr std::size_t maximum = static_cast<std::size_t>(INT64_MAX);

void Require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::abort(); }
}

void Oversized(int descriptor, const char* mode) {
    std::array<char, 8> buffer{'s', 'e', 'n', 't', 'i', 'n', 'e', 'l'};
    const auto original = buffer;
    for (const auto size : {maximum + 1, std::numeric_limits<std::size_t>::max()}) {
        *__error_nid_postfix() = 123;
        if (std::strcmp(mode, "read") == 0) Require(sceKernelRead(descriptor, buffer.data(), size) == invalid, "oversized read not EINVAL");
        else if (std::strcmp(mode, "write") == 0) Require(sceKernelWrite(descriptor, buffer.data(), size) == invalid, "oversized write not EINVAL");
        else if (std::strcmp(mode, "pread") == 0) Require(sceKernelPread(descriptor, buffer.data(), size, 0) == invalid, "oversized pread not EINVAL");
        else if (std::strcmp(mode, "pwrite") == 0) Require(sceKernelPwrite(descriptor, buffer.data(), size, 0) == invalid, "oversized pwrite not EINVAL");
        else Require(false, "unknown oversized case");
        Require(*__error_nid_postfix() == 123, "kernel error changed POSIX errno");
        Require(buffer == original, "rejected transfer touched data");
    }
}

void Posix(int descriptor) {
    char buffer = 'x';
    const auto size = maximum + 1;
    Require(read_nid_postfix(descriptor, &buffer, size) == -1 && *__error_nid_postfix() == 22, "read errno mismatch");
    Require(_read_nid_postfix(descriptor, &buffer, size) == -1 && *__error_nid_postfix() == 22, "_read errno mismatch");
    Require(_write_nid_postfix(descriptor, &buffer, size) == -1 && *__error_nid_postfix() == 22, "_write errno mismatch");
    Require(pread_nid_postfix(descriptor, &buffer, size, 0) == -1 && *__error_nid_postfix() == 22, "pread errno mismatch");
    Require(pwrite_nid_disambig1_nid_postfix(descriptor, &buffer, size, 0) == -1 && *__error_nid_postfix() == 22, "pwrite errno mismatch");
    Require(buffer == 'x', "POSIX rejection touched buffer");
}

void Ordinary(int descriptor) {
    Require(sceKernelWrite(descriptor, "abcdef", 6) == 6, "ordinary write failed");
    Require(sceKernelPwrite(descriptor, "XY", 2, 2) == 2, "positioned write failed");
    std::array<char, 8> buffer{};
    Require(sceKernelPread(descriptor, buffer.data(), 8, 0) == 6, "short EOF read count changed");
    Require(std::memcmp(buffer.data(), "abXYef", 6) == 0 && buffer[6] == 0, "positioned content or guard changed");
    Require(sceKernelRead(descriptor, buffer.data(), 1) == 0, "positioned operation changed current file offset");
    Require(sceKernelRead(descriptor, nullptr, 0) == 0, "zero read failed");
    Require(sceKernelWrite(descriptor, nullptr, 0) == 0, "zero write failed");
    Require(sceKernelPread(descriptor, nullptr, 0, 0) == 0, "zero pread failed");
    Require(sceKernelPwrite(descriptor, nullptr, 0, 0) == 0, "zero pwrite failed");
    Require(sceKernelRead(-1, buffer.data(), 1) == badDescriptor, "read lost EBADF");
    Require(sceKernelWrite(-1, buffer.data(), 1) == badDescriptor, "write lost EBADF");
    Require(sceKernelPread(-1, buffer.data(), 1, 0) == badDescriptor, "pread lost EBADF");
    Require(sceKernelPwrite(-1, buffer.data(), 1, 0) == badDescriptor, "pwrite lost EBADF");
}

void Boundaries() {
    Require(!File::ExceedsTransferLimit(maximum) && File::ExceedsTransferLimit(maximum + 1), "guest signed-size boundary wrong");
    constexpr auto chunk = static_cast<std::size_t>(INT32_MAX);
    for (const auto bytes : {std::size_t{0}, std::size_t{1}, chunk - 1, chunk, chunk + 1, std::size_t{UINT32_MAX}, std::size_t{UINT32_MAX} + 1, maximum}) {
        const auto expected = bytes < chunk ? bytes : chunk;
        Require(File::NativeIoChunk(bytes) == expected, "Windows native count narrowed or exceeded signed CRT limit");
    }
}

void ErrorMatrix(int descriptor) {
    constexpr int fault = static_cast<int>(0x8002000e);
    char sentinel = 'x';
    *__error_nid_postfix() = 123;
    Require(sceKernelRead(descriptor, nullptr, 1) == fault, "null read did not return EFAULT");
    Require(sceKernelWrite(descriptor, nullptr, 1) == fault, "null write did not return EFAULT");
    Require(sceKernelPread(descriptor, nullptr, 1, 0) == fault, "null pread did not return EFAULT");
    Require(sceKernelPwrite(descriptor, nullptr, 1, 0) == fault, "null pwrite did not return EFAULT");
    for (auto offset : {std::int64_t{-1}, std::numeric_limits<std::int64_t>::min()}) {
        Require(sceKernelPread(descriptor, &sentinel, 1, offset) == invalid, "negative pread offset accepted");
        Require(sceKernelPwrite(descriptor, &sentinel, 1, offset) == invalid, "negative pwrite offset accepted");
    }
    Require(*__error_nid_postfix() == 123, "kernel validation changed POSIX errno");
    Require(read_nid_postfix(descriptor, nullptr, 1) == -1 && *__error_nid_postfix() == 14, "null POSIX read errno changed");
    Require(_write_nid_postfix(descriptor, nullptr, 1) == -1 && *__error_nid_postfix() == 14, "null POSIX write errno changed");
    Require(pread_nid_postfix(descriptor, &sentinel, 1, -1) == -1 && *__error_nid_postfix() == 22, "negative POSIX pread errno changed");
    Require(pwrite_nid_disambig1_nid_postfix(descriptor, &sentinel, 1, -1) == -1 && *__error_nid_postfix() == 22, "negative POSIX pwrite errno changed");
    Require(sentinel == 'x', "failed I/O touched the buffer");
    Require(sceKernelRead(descriptor, &sentinel, 1) == 0, "rejected write changed empty file");
}

void VectorLimits(int descriptor, bool posix) {
    std::array<char, 8> sentinel{'s', 'e', 'n', 't', 'i', 'n', 'e', 'l'};
    const auto original = sentinel;
    for (const auto lengths : {std::array<std::size_t, 2>{maximum + 1, 0}, {maximum, 1}, {maximum / 2 + 1, maximum / 2 + 1}, {1, SIZE_MAX}}) {
        const std::array<GuestIovec, 2> vectors{{{sentinel.data(), lengths[0]}, {sentinel.data(), lengths[1]}}};
        for (int operation = 0; operation < 4; ++operation) {
            *__error_nid_postfix() = 123;
            std::int64_t result;
            if (posix) {
                if (operation == 0) result = readv_nid_postfix(descriptor, vectors.data(), 2);
                else if (operation == 1) result = writev_nid_postfix(descriptor, vectors.data(), 2);
                else if (operation == 2) result = preadv_nid_postfix(descriptor, vectors.data(), 2, 0);
                else result = pwritev_nid_postfix(descriptor, vectors.data(), 2, 0);
            } else {
                if (operation == 0) result = sceKernelReadv(descriptor, vectors.data(), 2);
                else if (operation == 1) result = sceKernelWritev(descriptor, vectors.data(), 2);
                else if (operation == 2) result = sceKernelPreadv(descriptor, vectors.data(), 2, 0);
                else result = sceKernelPwritev(descriptor, vectors.data(), 2, 0);
            }
            Require(result == (posix ? -1 : invalid), "vector aggregate overflow not rejected before dispatch");
            Require(*__error_nid_postfix() == (posix ? 22 : 123), "vector validation used wrong error channel");
            Require(sentinel == original, "rejected vector touched destination");
        }
    }
}

void VectorBoundaries(int descriptor) {
    char sentinel = 'x';
    std::array<GuestIovec, 1024> vectors{};
    vectors.back() = {&sentinel, 1};
    Require(sceKernelWritev(descriptor, vectors.data(), 1024) == 1, "maximum vector count rejected");
    Require(sceKernelPreadv(descriptor, vectors.data(), 1024, 0) == 1 && sentinel == 'x', "maximum read vector count rejected");
    for (const int count : {-1, 1025}) {
        Require(sceKernelReadv(descriptor, vectors.data(), count) == invalid, "invalid read vector count accepted");
        Require(sceKernelWritev(descriptor, vectors.data(), count) == invalid, "invalid write vector count accepted");
        Require(sceKernelPreadv(descriptor, vectors.data(), count, 0) == invalid, "invalid positioned read count accepted");
        Require(sceKernelPwritev(descriptor, vectors.data(), count, 0) == invalid, "invalid positioned write count accepted");
    }
    Require(sceKernelWritev(descriptor, nullptr, 0) == 0 && sceKernelReadv(descriptor, nullptr, 0) == 0, "empty vector touched null array");
    Require(sceKernelPwritev(descriptor, nullptr, 0, 0) == 0 && sceKernelPreadv(descriptor, nullptr, 0, 0) == 0, "empty positioned vector failed");
    Require(sceKernelRead(descriptor, &sentinel, 1) == 0, "positioned vector moved current offset");
}

}

int main(int argc, char** argv) {
    Require(argc == 2, "expected I/O limit case");
    auto* file = std::tmpfile();
    Require(file != nullptr, "temporary file failed");
#ifdef _WIN32
    const int descriptor = ::_fileno(file);
#else
    const int descriptor = ::fileno(file);
#endif
    if (std::strcmp(argv[1], "ordinary") == 0) Ordinary(descriptor);
    else if (std::strcmp(argv[1], "posix") == 0) Posix(descriptor);
    else if (std::strcmp(argv[1], "boundaries") == 0) Boundaries();
    else if (std::strcmp(argv[1], "error-matrix") == 0) ErrorMatrix(descriptor);
    else if (std::strcmp(argv[1], "vector-limits") == 0) VectorLimits(descriptor, false);
    else if (std::strcmp(argv[1], "vector-posix") == 0) VectorLimits(descriptor, true);
    else if (std::strcmp(argv[1], "vector-boundaries") == 0) VectorBoundaries(descriptor);
    else if (std::strcmp(argv[1], "vector-random") == 0) {
        const int random = sceKernelOpen("/dev/urandom", 0, 0);
        Require(random >= 0, "random descriptor open failed");
        VectorLimits(random, false);
        VectorLimits(random, true);
        Require(sceKernelClose(random) == 0, "random descriptor close failed");
    }
    else Oversized(descriptor, argv[1]);
    Require(std::fclose(file) == 0, "fixture close failed");
}
