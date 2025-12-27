/**
 * @file TestFramework.h
 * @brief Minimal C++11 test framework for CKImageReader
 *
 * Provides:
 * - CRC32 computation for pixel data validation
 * - Test registration and execution macros
 * - Assertion helpers with detailed failure reporting
 * - Reference filename parsing (extracts expected CRC)
 */

#ifndef TESTFRAMEWORK_H
#define TESTFRAMEWORK_H

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#include <sstream>
#include <iomanip>

// For file system operations - use minimal Windows headers to avoid conflicts
// with bitmap structures defined in reader headers
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOGDI
#define NOGDI
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

namespace TestFramework {

//=============================================================================
// CRC32 Implementation (IEEE 802.3 polynomial)
//=============================================================================

class CRC32 {
public:
    static uint32_t compute(const void* data, size_t length) {
        static const uint32_t table[256] = {
            0x00000000L, 0x77073096L, 0xee0e612cL, 0x990951baL, 0x076dc419L,
            0x706af48fL, 0xe963a535L, 0x9e6495a3L, 0x0edb8832L, 0x79dcb8a4L,
            0xe0d5e91eL, 0x97d2d988L, 0x09b64c2bL, 0x7eb17cbdL, 0xe7b82d07L,
            0x90bf1d91L, 0x1db71064L, 0x6ab020f2L, 0xf3b97148L, 0x84be41deL,
            0x1adad47dL, 0x6ddde4ebL, 0xf4d4b551L, 0x83d385c7L, 0x136c9856L,
            0x646ba8c0L, 0xfd62f97aL, 0x8a65c9ecL, 0x14015c4fL, 0x63066cd9L,
            0xfa0f3d63L, 0x8d080df5L, 0x3b6e20c8L, 0x4c69105eL, 0xd56041e4L,
            0xa2677172L, 0x3c03e4d1L, 0x4b04d447L, 0xd20d85fdL, 0xa50ab56bL,
            0x35b5a8faL, 0x42b2986cL, 0xdbbbc9d6L, 0xacbcf940L, 0x32d86ce3L,
            0x45df5c75L, 0xdcd60dcfL, 0xabd13d59L, 0x26d930acL, 0x51de003aL,
            0xc8d75180L, 0xbfd06116L, 0x21b4f4b5L, 0x56b3c423L, 0xcfba9599L,
            0xb8bda50fL, 0x2802b89eL, 0x5f058808L, 0xc60cd9b2L, 0xb10be924L,
            0x2f6f7c87L, 0x58684c11L, 0xc1611dabL, 0xb6662d3dL, 0x76dc4190L,
            0x01db7106L, 0x98d220bcL, 0xefd5102aL, 0x71b18589L, 0x06b6b51fL,
            0x9fbfe4a5L, 0xe8b8d433L, 0x7807c9a2L, 0x0f00f934L, 0x9609a88eL,
            0xe10e9818L, 0x7f6a0dbbL, 0x086d3d2dL, 0x91646c97L, 0xe6635c01L,
            0x6b6b51f4L, 0x1c6c6162L, 0x856530d8L, 0xf262004eL, 0x6c0695edL,
            0x1b01a57bL, 0x8208f4c1L, 0xf50fc457L, 0x65b0d9c6L, 0x12b7e950L,
            0x8bbeb8eaL, 0xfcb9887cL, 0x62dd1ddfL, 0x15da2d49L, 0x8cd37cf3L,
            0xfbd44c65L, 0x4db26158L, 0x3ab551ceL, 0xa3bc0074L, 0xd4bb30e2L,
            0x4adfa541L, 0x3dd895d7L, 0xa4d1c46dL, 0xd3d6f4fbL, 0x4369e96aL,
            0x346ed9fcL, 0xad678846L, 0xda60b8d0L, 0x44042d73L, 0x33031de5L,
            0xaa0a4c5fL, 0xdd0d7cc9L, 0x5005713cL, 0x270241aaL, 0xbe0b1010L,
            0xc90c2086L, 0x5768b525L, 0x206f85b3L, 0xb966d409L, 0xce61e49fL,
            0x5edef90eL, 0x29d9c998L, 0xb0d09822L, 0xc7d7a8b4L, 0x59b33d17L,
            0x2eb40d81L, 0xb7bd5c3bL, 0xc0ba6cadL, 0xedb88320L, 0x9abfb3b6L,
            0x03b6e20cL, 0x74b1d29aL, 0xead54739L, 0x9dd277afL, 0x04db2615L,
            0x73dc1683L, 0xe3630b12L, 0x94643b84L, 0x0d6d6a3eL, 0x7a6a5aa8L,
            0xe40ecf0bL, 0x9309ff9dL, 0x0a00ae27L, 0x7d079eb1L, 0xf00f9344L,
            0x8708a3d2L, 0x1e01f268L, 0x6906c2feL, 0xf762575dL, 0x806567cbL,
            0x196c3671L, 0x6e6b06e7L, 0xfed41b76L, 0x89d32be0L, 0x10da7a5aL,
            0x67dd4accL, 0xf9b9df6fL, 0x8ebeeff9L, 0x17b7be43L, 0x60b08ed5L,
            0xd6d6a3e8L, 0xa1d1937eL, 0x38d8c2c4L, 0x4fdff252L, 0xd1bb67f1L,
            0xa6bc5767L, 0x3fb506ddL, 0x48b2364bL, 0xd80d2bdaL, 0xaf0a1b4cL,
            0x36034af6L, 0x41047a60L, 0xdf60efc3L, 0xa867df55L, 0x316e8eefL,
            0x4669be79L, 0xcb61b38cL, 0xbc66831aL, 0x256fd2a0L, 0x5268e236L,
            0xcc0c7795L, 0xbb0b4703L, 0x220216b9L, 0x5505262fL, 0xc5ba3bbeL,
            0xb2bd0b28L, 0x2bb45a92L, 0x5cb36a04L, 0xc2d7ffa7L, 0xb5d0cf31L,
            0x2cd99e8bL, 0x5bdeae1dL, 0x9b64c2b0L, 0xec63f226L, 0x756aa39cL,
            0x026d930aL, 0x9c0906a9L, 0xeb0e363fL, 0x72076785L, 0x05005713L,
            0x95bf4a82L, 0xe2b87a14L, 0x7bb12baeL, 0x0cb61b38L, 0x92d28e9bL,
            0xe5d5be0dL, 0x7cdcefb7L, 0x0bdbdf21L, 0x86d3d2d4L, 0xf1d4e242L,
            0x68ddb3f8L, 0x1fda836eL, 0x81be16cdL, 0xf6b9265bL, 0x6fb077e1L,
            0x18b74777L, 0x88085ae6L, 0xff0f6a70L, 0x66063bcaL, 0x11010b5cL,
            0x8f659effL, 0xf862ae69L, 0x616bffd3L, 0x166ccf45L, 0xa00ae278L,
            0xd70dd2eeL, 0x4e048354L, 0x3903b3c2L, 0xa7672661L, 0xd06016f7L,
            0x4969474dL, 0x3e6e77dbL, 0xaed16a4aL, 0xd9d65adcL, 0x40df0b66L,
            0x37d83bf0L, 0xa9bcae53L, 0xdebb9ec5L, 0x47b2cf7fL, 0x30b5ffe9L,
            0xbdbdf21cL, 0xcabac28aL, 0x53b39330L, 0x24b4a3a6L, 0xbad03605L,
            0xcdd70693L, 0x54de5729L, 0x23d967bfL, 0xb3667a2eL, 0xc4614ab8L,
            0x5d681b02L, 0x2a6f2b94L, 0xb40bbe37L, 0xc30c8ea1L, 0x5a05df1bL,
            0x2d02ef8dL
        };

        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        uint32_t crc = 0xFFFFFFFF;
        for (size_t i = 0; i < length; ++i) {
            crc = table[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);
        }
        return crc ^ 0xFFFFFFFF;
    }

    static std::string toHex(uint32_t crc) {
        std::ostringstream oss;
        oss << std::hex << std::nouppercase << crc;
        return oss.str();
    }

    static uint32_t fromHex(const std::string& hex) {
        uint32_t result = 0;
        std::istringstream iss(hex);
        iss >> std::hex >> result;
        return result;
    }
};

//=============================================================================
// Test Result
//=============================================================================

enum class TestStatus {
    Passed,
    Failed,
    Skipped
};

struct TestResult {
    std::string name;
    TestStatus status;
    std::string message;
    double durationMs;

    TestResult() : status(TestStatus::Skipped), durationMs(0) {}
};

//=============================================================================
// Test Case
//=============================================================================

typedef std::function<void()> TestFunc;

struct TestCase {
    std::string name;
    std::string suite;
    TestFunc func;
    bool expectFailure;  // For negative tests

    TestCase(const std::string& n, const std::string& s, TestFunc f, bool expectFail = false)
        : name(n), suite(s), func(f), expectFailure(expectFail) {}
};

//=============================================================================
// Test Registry (singleton)
//=============================================================================

class TestRegistry {
public:
    static TestRegistry& instance() {
        static TestRegistry reg;
        return reg;
    }

    void registerTest(const TestCase& tc) {
        tests_.push_back(tc);
    }

    const std::vector<TestCase>& tests() const { return tests_; }

    // Current test context for assertions
    TestResult* currentResult;
    bool currentFailed;

private:
    TestRegistry() : currentResult(nullptr), currentFailed(false) {}
    std::vector<TestCase> tests_;
};

//=============================================================================
// Auto-registration helper
//=============================================================================

struct TestRegistrar {
    TestRegistrar(const std::string& name, const std::string& suite, TestFunc func, bool expectFail = false) {
        TestRegistry::instance().registerTest(TestCase(name, suite, func, expectFail));
    }
};

//=============================================================================
// Test Macros
//=============================================================================

#define TEST_CONCAT_(a, b) a##b
#define TEST_CONCAT(a, b) TEST_CONCAT_(a, b)

#define TEST(suite, name) \
    static void TEST_CONCAT(test_, __LINE__)(); \
    static TestFramework::TestRegistrar TEST_CONCAT(reg_, __LINE__)(#name, #suite, TEST_CONCAT(test_, __LINE__)); \
    static void TEST_CONCAT(test_, __LINE__)()

#define TEST_NEGATIVE(suite, name) \
    static void TEST_CONCAT(test_, __LINE__)(); \
    static TestFramework::TestRegistrar TEST_CONCAT(reg_, __LINE__)(#name, #suite, TEST_CONCAT(test_, __LINE__), true); \
    static void TEST_CONCAT(test_, __LINE__)()

//=============================================================================
// Assertion Helpers
//=============================================================================

inline void failTest(const std::string& msg) {
    TestRegistry& reg = TestRegistry::instance();
    reg.currentFailed = true;
    if (reg.currentResult) {
        reg.currentResult->status = TestStatus::Failed;
        reg.currentResult->message = msg;
    }
}

inline void skipTest(const std::string& msg) {
    TestRegistry& reg = TestRegistry::instance();
    if (reg.currentResult && !reg.currentFailed) {
        reg.currentResult->status = TestStatus::Skipped;
        reg.currentResult->message = msg;
    }
}

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::ostringstream oss; \
            oss << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__; \
            TestFramework::failTest(oss.str()); \
            return; \
        } \
    } while(0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_EQ(expected, actual) \
    do { \
        auto e_ = (expected); \
        auto a_ = (actual); \
        if (e_ != a_) { \
            std::ostringstream oss; \
            oss << "Assertion failed: expected " << e_ << " but got " << a_ \
                << " at " << __FILE__ << ":" << __LINE__; \
            TestFramework::failTest(oss.str()); \
            return; \
        } \
    } while(0)

#define ASSERT_GUID_EQ(expected, actual) \
    do { \
        auto e_ = (expected); \
        auto a_ = (actual); \
        if (e_.d1 != a_.d1 || e_.d2 != a_.d2) { \
            std::ostringstream oss; \
            oss << "GUID mismatch: expected {" << std::hex << e_.d1 << "," << e_.d2 \
                << "} but got {" << a_.d1 << "," << a_.d2 << "}" \
                << " at " << __FILE__ << ":" << __LINE__; \
            TestFramework::failTest(oss.str()); \
            return; \
        } \
    } while(0)

#define ASSERT_NE(expected, actual) \
    do { \
        auto e_ = (expected); \
        auto a_ = (actual); \
        if (e_ == a_) { \
            std::ostringstream oss; \
            oss << "Assertion failed: expected != " << e_ \
                << " at " << __FILE__ << ":" << __LINE__; \
            TestFramework::failTest(oss.str()); \
            return; \
        } \
    } while(0)

#define ASSERT_CRC(expectedCrc, data, size) \
    do { \
        uint32_t actualCrc = TestFramework::CRC32::compute(data, size); \
        if (expectedCrc != actualCrc) { \
            std::ostringstream oss; \
            oss << "CRC mismatch: expected " << std::hex << expectedCrc \
                << " but got " << actualCrc \
                << " at " << __FILE__ << ":" << __LINE__; \
            TestFramework::failTest(oss.str()); \
            return; \
        } \
    } while(0)

#define SKIP_TEST(msg) \
    do { \
        TestFramework::skipTest(msg); \
        return; \
    } while(0)

//=============================================================================
// File System Helpers
//=============================================================================

inline bool fileExists(const std::string& path) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;
    return (stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode));
#endif
}

inline bool directoryExists(const std::string& path) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;
    return (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
#endif
}

inline std::vector<std::string> listDirectory(const std::string& path) {
    std::vector<std::string> result;
#ifdef _WIN32
    WIN32_FIND_DATAA ffd;
    std::string searchPath = path + "\\*";
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &ffd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            std::string name = ffd.cFileName;
            if (name != "." && name != "..") {
                result.push_back(name);
            }
        } while (FindNextFileA(hFind, &ffd));
        FindClose(hFind);
    }
#else
    DIR* dir = opendir(path.c_str());
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name != "." && name != "..") {
                result.push_back(name);
            }
        }
        closedir(dir);
    }
#endif
    std::sort(result.begin(), result.end());
    return result;
}

inline std::string getFileName(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

inline std::string getExtension(const std::string& path) {
    std::string fname = getFileName(path);
    size_t pos = fname.rfind('.');
    return (pos == std::string::npos) ? "" : fname.substr(pos);
}

inline std::string toLower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

//=============================================================================
// Reference File Parsing
// Format: <inputname>.<crc_hex>.png
// Example: rgb24.bmp.25bba0a.png -> input="rgb24.bmp", crc=0x25bba0a
//=============================================================================

struct ReferenceInfo {
    std::string inputName;
    uint32_t expectedCrc;
    bool valid;

    ReferenceInfo() : expectedCrc(0), valid(false) {}
};

inline ReferenceInfo parseReferenceFilename(const std::string& refFilename) {
    ReferenceInfo info;

    // Strip .png/.tiff extension
    std::string name = refFilename;
    if (toLower(getExtension(name)) == ".png" || toLower(getExtension(name)) == ".tiff") {
        size_t pos = name.rfind('.');
        if (pos != std::string::npos) name = name.substr(0, pos);
    }

    // Now name is like "rgb24.bmp.25bba0a"
    // Find the last dot to extract CRC
    size_t lastDot = name.rfind('.');
    if (lastDot == std::string::npos) return info;

    std::string crcStr = name.substr(lastDot + 1);
    info.inputName = name.substr(0, lastDot);
    info.expectedCrc = CRC32::fromHex(crcStr);
    info.valid = true;
    return info;
}

//=============================================================================
// Binary File I/O
//=============================================================================

inline std::vector<uint8_t> readBinaryFile(const std::string& path) {
    std::vector<uint8_t> data;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return data;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size > 0) {
        data.resize(static_cast<size_t>(size));
        fread(data.data(), 1, data.size(), f);
    }
    fclose(f);
    return data;
}

inline bool writeBinaryFile(const std::string& path, const void* data, size_t size) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    size_t written = fwrite(data, 1, size, f);
    fclose(f);
    return written == size;
}

//=============================================================================
// Path Helpers
//=============================================================================

inline std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    char lastChar = a[a.size() - 1];
    if (lastChar == '/' || lastChar == '\\') {
        return a + b;
    }
#ifdef _WIN32
    return a + "\\" + b;
#else
    return a + "/" + b;
#endif
}

//=============================================================================
// Corpus Iteration Helpers
// Utilities for table-driven fixture tests
//=============================================================================

// Check if a filename has a given extension (case-insensitive)
inline bool hasExtension(const std::string& filename, const std::string& ext) {
    std::string fileExt = getExtension(filename);
    std::string extLower = toLower(fileExt);
    std::string targetLower = toLower(ext);
    // Handle both ".bmp" and "bmp" forms
    if (!targetLower.empty() && targetLower[0] != '.') {
        targetLower = "." + targetLower;
    }
    return extLower == targetLower;
}

// Check if filename matches any of the given extensions
inline bool hasAnyExtension(const std::string& filename, const std::vector<std::string>& exts) {
    for (size_t i = 0; i < exts.size(); ++i) {
        if (hasExtension(filename, exts[i])) return true;
    }
    return false;
}

// Collect all files with given extension(s) from a directory
inline std::vector<std::string> collectFilesWithExtension(const std::string& dir, const std::string& ext) {
    std::vector<std::string> result;
    if (!directoryExists(dir)) return result;
    
    std::vector<std::string> entries = listDirectory(dir);
    for (size_t i = 0; i < entries.size(); ++i) {
        const std::string& entry = entries[i];
        std::string fullPath = joinPath(dir, entry);
        if (fileExists(fullPath) && hasExtension(entry, ext)) {
            result.push_back(entry);
        }
    }
    return result;
}

// Collect all files with any of the given extensions
inline std::vector<std::string> collectFilesWithExtensions(const std::string& dir, const std::vector<std::string>& exts) {
    std::vector<std::string> result;
    if (!directoryExists(dir)) return result;
    
    std::vector<std::string> entries = listDirectory(dir);
    for (size_t i = 0; i < entries.size(); ++i) {
        const std::string& entry = entries[i];
        std::string fullPath = joinPath(dir, entry);
        if (fileExists(fullPath) && hasAnyExtension(entry, exts)) {
            result.push_back(entry);
        }
    }
    return result;
}

// Structure to hold corpus test results for reporting
struct CorpusTestStats {
    int total;
    int passed;
    int failed;
    int skipped;
    std::vector<std::string> failures;
    
    CorpusTestStats() : total(0), passed(0), failed(0), skipped(0) {}
    
    void recordPass() { ++total; ++passed; }
    void recordFail(const std::string& filename, const std::string& reason) {
        ++total; ++failed;
        failures.push_back(filename + ": " + reason);
    }
    void recordSkip() { ++total; ++skipped; }
    
    bool allPassed() const { return failed == 0; }
    
    std::string summary() const {
        std::ostringstream oss;
        oss << "Corpus: " << passed << "/" << total << " passed";
        if (skipped > 0) oss << ", " << skipped << " skipped";
        if (failed > 0) oss << ", " << failed << " FAILED";
        return oss.str();
    }
};

// Callback type for corpus iteration
typedef std::function<bool(const std::string& filepath, const std::string& filename)> CorpusTestFunc;

// Iterate over all files in directory with given extension, calling testFunc for each
// Returns corpus stats; testFunc should return true on success, false on failure
inline CorpusTestStats runCorpusTests(const std::string& dir, const std::string& ext, CorpusTestFunc testFunc) {
    CorpusTestStats stats;
    std::vector<std::string> files = collectFilesWithExtension(dir, ext);
    
    for (size_t i = 0; i < files.size(); ++i) {
        const std::string& filename = files[i];
        std::string filepath = joinPath(dir, filename);
        
        try {
            if (testFunc(filepath, filename)) {
                stats.recordPass();
            } else {
                stats.recordFail(filename, "test returned false");
            }
        } catch (const std::exception& e) {
            stats.recordFail(filename, std::string("exception: ") + e.what());
        } catch (...) {
            stats.recordFail(filename, "unknown exception");
        }
    }
    
    return stats;
}

} // namespace TestFramework

// Provide global path access via using
using TestFramework::joinPath;
using TestFramework::fileExists;
using TestFramework::directoryExists;
using TestFramework::listDirectory;
using TestFramework::parseReferenceFilename;
using TestFramework::readBinaryFile;
using TestFramework::writeBinaryFile;
using TestFramework::getFileName;
using TestFramework::toLower;
using TestFramework::getExtension;
using TestFramework::hasExtension;
using TestFramework::hasAnyExtension;
using TestFramework::collectFilesWithExtension;
using TestFramework::collectFilesWithExtensions;
using TestFramework::CorpusTestStats;
using TestFramework::CorpusTestFunc;
using TestFramework::runCorpusTests;

//=============================================================================
// Global Test Directories (defined in TestMain.cpp)
//=============================================================================

extern const char* g_TestImagesDir;
extern const char* g_TestReferenceDir;
extern const char* g_TestOutputDir;

//=============================================================================
// Reference CRC lookup (defined in TestMain.cpp)
//=============================================================================

// Get CKImageReader-specific reference CRC for a test image
// key format: "bmp/<filename>" or "tga/encoding/<filename>" etc.
extern bool getReferenceCrc(const std::string& key, uint32_t& outCrc);

#endif // TESTFRAMEWORK_H
