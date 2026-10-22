#include "prx/libc/include/FileStream.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include <array>
#include <atomic>
#include <barrier>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <string>
#include <memory>
#include <filesystem>
#include <chrono>
#include <thread>
#include <vector>

extern "C" {
int APS5_VABI fputc_nid_postfix(int, FileStream*);
int APS5_VABI putc_unlocked_nid_postfix(int, FileStream*);
int APS5_VABI ferror_nid_postfix(FileStream*);
int APS5_VABI feof_nid_postfix(FileStream*);
int APS5_VABI fileno_nid_postfix(FileStream*);
void APS5_VABI clearerr_nid_postfix(FileStream*);
void APS5_VABI clearerr_unlocked_nid_postfix(FileStream*);
std::int32_t APS5_VABI fgetwc_nid_postfix(FileStream*);
char16_t* APS5_VABI fgetws_nid_postfix(char16_t*, int, FileStream*);
int APS5_VABI fscanf_nid_postfix(FileStream*, const char*, ...);
int APS5_VABI vfscanf_nid_postfix(FileStream*, const char*, void*);
int APS5_VABI fprintf_nid_postfix(FileStream*, const char*, ...);
int APS5_VABI puts_nid_postfix(const char*);
int APS5_VABI fputwc_nid_postfix(char16_t, FileStream*);
int APS5_VABI fputws_nid_postfix(const char16_t*, FileStream*);
int APS5_VABI fwprintf_nid_postfix(FileStream*, const char16_t*, ...);
}

namespace {

void Require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::abort(); }
}

void Status(FileStream& stream) {
    std::barrier start(4);
    std::vector<std::thread> workers;
    for (int worker = 0; worker < 4; ++worker) workers.emplace_back([&, worker] {
        start.arrive_and_wait();
        for (int iteration = 0; iteration < 10000; ++iteration) {
            if (worker == 0) stream.SetError();
            else if (worker == 1) clearerr_nid_postfix(&stream);
            else if (worker == 2) {
                Require(ferror_nid_postfix(&stream) >= 0, "error status invalid");
                Require(feof_nid_postfix(&stream) == 0, "empty update stream unexpectedly at EOF");
            } else {
                stream.SyncStatus();
                Require(fileno_nid_postfix(&stream) >= 0, "descriptor lost during status update");
            }
        }
    });
    for (auto& worker : workers) worker.join();
    stream.SetError();
    Require(ferror_nid_postfix(&stream) == 1, "sticky error lost");
    stream.SyncStatus();
    Require(ferror_nid_postfix(&stream) == 1, "sync cleared sticky error");
    clearerr_nid_postfix(&stream);
    Require(ferror_nid_postfix(&stream) == 0, "clear did not reset sticky error");
}

void Writers(FileStream& stream) {
    constexpr int iterations = 5000;
    std::barrier start(4);
    std::vector<std::thread> workers;
    for (int worker = 0; worker < 4; ++worker) workers.emplace_back([&, worker] {
        start.arrive_and_wait();
        for (int iteration = 0; iteration < iterations; ++iteration) {
            if (worker % 2 == 0) Require(fputc_nid_postfix('A' + worker, &stream) == 'A' + worker, "locked write failed");
            else {
#ifdef _WIN32
                ::_lock_file(stream.GetHandle());
#else
                ::flockfile(stream.GetHandle());
#endif
                Require(putc_unlocked_nid_postfix('A' + worker, &stream) == 'A' + worker, "unlocked write under caller lock failed");
                clearerr_unlocked_nid_postfix(&stream);
#ifdef _WIN32
                ::_unlock_file(stream.GetHandle());
#else
                ::funlockfile(stream.GetHandle());
#endif
            }
        }
    });
    for (auto& worker : workers) worker.join();
    Require(std::fflush(stream.GetHandle()) == 0, "flush failed");
    std::rewind(stream.GetHandle());
    std::array<int, 4> counts{};
    for (int character; (character = std::fgetc(stream.GetHandle())) != EOF;) {
        Require(character >= 'A' && character <= 'D', "corrupt output byte");
        ++counts[character - 'A'];
    }
    for (int count : counts) Require(count == iterations, "concurrent writer lost bytes");
    Require(ferror_nid_postfix(&stream) == 0, "successful writers set stream error");
}

void WideReaders(FileStream& stream) {
    constexpr int characters = 20000;
    for (int index = 0; index < characters; ++index) {
        Require(std::fputc(0xc2, stream.GetHandle()) == 0xc2, "fixture lead write failed");
        Require(std::fputc(0xa9, stream.GetHandle()) == 0xa9, "fixture continuation write failed");
    }
    std::rewind(stream.GetHandle());
    std::barrier start(4);
    std::atomic<int> count{0};
    std::atomic<int> invalid{0};
    std::vector<std::thread> workers;
    for (int worker = 0; worker < 4; ++worker) workers.emplace_back([&] {
        start.arrive_and_wait();
        for (;;) {
            const auto value = fgetwc_nid_postfix(&stream);
            if (value == -1) break;
            if (value != 0xa9) ++invalid;
            ++count;
        }
    });
    for (auto& worker : workers) worker.join();
    Require(count == characters && invalid == 0, "wide-character bytes interleaved between readers");
    Require(feof_nid_postfix(&stream) == 1 && ferror_nid_postfix(&stream) == 0, "concurrent wide reads corrupted EOF/error state");
}

void Closed(FileStream& stream) {
    stream.Detach();
    errno = 0;
    Require(fgetwc_nid_postfix(&stream) == -1 && errno == 9, "closed wide input changed errno behavior");
    Require(ferror_nid_postfix(&stream) == 0 && feof_nid_postfix(&stream) == 0, "closed status was not cleared");
    clearerr_nid_postfix(&stream);
}

void WideLines(FileStream& stream) {
    constexpr int lines = 4000;
    for (int line = 0; line < lines; ++line) {
        for (int column = 0; column < 31; ++column) {
            Require(std::fputc(0xc2, stream.GetHandle()) == 0xc2, "line lead write failed");
            Require(std::fputc(0xa9, stream.GetHandle()) == 0xa9, "line continuation write failed");
        }
        Require(std::fputc('\n', stream.GetHandle()) == '\n', "line ending write failed");
    }
    std::rewind(stream.GetHandle());
    std::barrier start(4);
    std::atomic<int> count{0};
    std::vector<std::thread> workers;
    for (int worker = 0; worker < 4; ++worker) workers.emplace_back([&] {
        start.arrive_and_wait();
        std::array<char16_t, 64> buffer{};
        while (fgetws_nid_postfix(buffer.data(), static_cast<int>(buffer.size()), &stream)) {
            for (int column = 0; column < 31; ++column)
                Require(buffer[column] == 0xa9, "wide line split between readers");
            Require(buffer[31] == u'\n' && buffer[32] == 0, "wide line boundary corrupted");
            ++count;
        }
    });
    for (auto& worker : workers) worker.join();
    Require(count == lines && ferror_nid_postfix(&stream) == 0, "wide lines lost or stream error set");
}

int APS5_VABI ScanArgs(FileStream* stream, const char* format, ...) {
#ifdef _WIN32
    __builtin_sysv_va_list args;
    __builtin_sysv_va_start(args, format);
#else
    std::va_list args;
    va_start(args, format);
#endif
    const int result = vfscanf_nid_postfix(stream, format, args);
#ifdef _WIN32
    __builtin_sysv_va_end(args);
#else
    va_end(args);
#endif
    return result;
}

void ScanRecords(FileStream& stream, bool variadic) {
    constexpr int records = 6000;
    for (int record = 0; record < records; ++record)
        Require(std::fprintf(stream.GetHandle(), "%d:%d;", record, record + 10000) > 0, "scan seed failed");
    std::rewind(stream.GetHandle());
    std::array<std::atomic<int>, records> seen{};
    std::barrier start(4);
    std::vector<std::thread> workers;
    for (int worker = 0; worker < 4; ++worker) workers.emplace_back([&] {
        start.arrive_and_wait();
        for (;;) {
            int first = -1;
            int second = -1;
            const int count = variadic ? ScanArgs(&stream, "%d:%d;", &first, &second)
                                       : fscanf_nid_postfix(&stream, "%d:%d;", &first, &second);
            if (count == EOF) break;
            Require(count == 2 && first >= 0 && first < records && second == first + 10000, "formatted record split between readers");
            ++seen[first];
        }
    });
    for (auto& worker : workers) worker.join();
    for (const auto& count : seen) Require(count == 1, "formatted record lost or duplicated");
    Require(feof_nid_postfix(&stream) == 1 && ferror_nid_postfix(&stream) == 0, "scan status lost");
}

void OutputRecords(FileStream& stream, bool standard) {
    auto* saved = standard ? _Stdout_nid_postfix.GetHandle() : nullptr;
    if (standard) {
        std::destroy_at(&_Stdout_nid_postfix);
        std::construct_at(&_Stdout_nid_postfix, stream.GetHandle());
    }
    constexpr int iterations = 4000;
    std::barrier start(4);
    std::vector<std::thread> workers;
    for (int worker = 0; worker < 4; ++worker) workers.emplace_back([&, worker] {
        const std::string record(63, static_cast<char>('A' + worker));
        start.arrive_and_wait();
        for (int iteration = 0; iteration < iterations; ++iteration) {
            const int result = standard ? puts_nid_postfix(record.c_str()) : fprintf_nid_postfix(&stream, "%s\n", record.c_str());
            Require(result >= 0, "formatted write failed");
        }
    });
    for (auto& worker : workers) worker.join();
    if (standard) {
        std::destroy_at(&_Stdout_nid_postfix);
        std::construct_at(&_Stdout_nid_postfix, saved);
    }
    std::rewind(stream.GetHandle());
    std::array<int, 4> counts{};
    std::array<char, 256> line{};
    while (std::fgets(line.data(), static_cast<int>(line.size()), stream.GetHandle())) {
        Require(std::strlen(line.data()) == 64 && line[63] == '\n', "output record boundary interleaved");
        Require(line[0] >= 'A' && line[0] <= 'D', "unexpected record writer");
        for (int column = 1; column < 63; ++column) Require(line[column] == line[0], "record payload interleaved");
        ++counts[line[0] - 'A'];
    }
    for (int count : counts) Require(count == iterations, "output record lost");
}

void PutsStatus(FileStream& stream) {
    auto* saved = _Stdout_nid_postfix.GetHandle();
    std::destroy_at(&_Stdout_nid_postfix);
    std::construct_at(&_Stdout_nid_postfix, stream.GetHandle());
    auto& guest = _Stdout_nid_postfix.GuestState();
    guest.readRemaining = 13;
    guest.writeRemaining = 19;
    Require(puts_nid_postfix("status") >= 0, "puts failed");
    Require(guest.readRemaining == 0 && guest.writeRemaining == 0, "puts left stale guest buffer counters");
    std::destroy_at(&_Stdout_nid_postfix);
    std::construct_at(&_Stdout_nid_postfix, saved);
}

void WideWriteErrors() {
    const auto name = "anyps5-wide-write-errors-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    auto* seed = std::fopen(name.c_str(), "wb");
    Require(seed && std::fputs("original", seed) >= 0 && std::fclose(seed) == 0, "read-only fixture seed failed");
    FileStream readOnly(std::fopen(name.c_str(), "rb"));
    for (int operation = 0; operation < 3; ++operation) {
        clearerr_nid_postfix(&readOnly);
        const int result = operation == 0 ? fputwc_nid_postfix(u'X', &readOnly)
                         : operation == 1 ? fputws_nid_postfix(u"XYZ", &readOnly)
                                          : fwprintf_nid_postfix(&readOnly, u"%d", 42);
        Require(result == -1, "read-only wide write succeeded");
        Require((readOnly.GuestState().flags & 0x40) != 0, "wide write failure did not publish guest error flag");
        Require(ferror_nid_postfix(&readOnly) == 1, "wide write lost sticky error");
    }
    readOnly.Close();
    Require(std::filesystem::file_size(name) == 8 && std::filesystem::remove(name), "read-only wide write altered file");
}

}

int main(int argc, char** argv) {
    Require(argc == 2, "expected stream locking case");
    auto* native = std::tmpfile();
    Require(native != nullptr, "tmpfile failed");
    FileStream stream(native);
    if (std::strcmp(argv[1], "status") == 0) Status(stream);
    else if (std::strcmp(argv[1], "writers") == 0) Writers(stream);
    else if (std::strcmp(argv[1], "wide-readers") == 0) WideReaders(stream);
    else if (std::strcmp(argv[1], "closed") == 0) Closed(stream);
    else if (std::strcmp(argv[1], "wide-lines") == 0) WideLines(stream);
    else if (std::strcmp(argv[1], "scan-records") == 0) ScanRecords(stream, false);
    else if (std::strcmp(argv[1], "vscan-records") == 0) ScanRecords(stream, true);
    else if (std::strcmp(argv[1], "formatted-writers") == 0) OutputRecords(stream, false);
    else if (std::strcmp(argv[1], "puts-lines") == 0) OutputRecords(stream, true);
    else if (std::strcmp(argv[1], "puts-status") == 0) PutsStatus(stream);
    else if (std::strcmp(argv[1], "wide-write-errors") == 0) WideWriteErrors();
    else Require(false, "unknown stream locking case");
    Require(std::fclose(native) == 0, "fixture close failed");
}
