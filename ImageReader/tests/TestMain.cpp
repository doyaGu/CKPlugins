/**
 * @file TestMain.cpp
 * @brief Main test driver for CKImageReader
 *
 * This file provides:
 * - Test runner that discovers and executes all registered tests
 * - Summary reporting with pass/fail/skip counts
 */

// Override VX_EXPORT to not use dllimport - we provide our own implementations
#define VX_EXPORT

#include "TestFramework.h"
#include <cstdio>
#include <ctime>
#include <cstring>
#include <cstdlib>

//=============================================================================
// Virtools memory function stubs for standalone testing
// The Virtools SDK uses mynew/mydelete for XString allocations
//=============================================================================

void* mynew(unsigned int size) {
    return malloc(size);
}

void mydelete(void* ptr) {
    free(ptr);
}

void* mynewrarray(unsigned int size) {
    return malloc(size);
}

void mydeletearray(void* ptr) {
    free(ptr);
}

void* VxNewAligned(int size, int align) {
    // Simple implementation - just allocate with extra space for alignment
    return _aligned_malloc(size, align);
}

void VxDeleteAligned(void* ptr) {
    _aligned_free(ptr);
}

// Include the actual reader headers (they include Virtools SDK types)
#include "ImageReader.h"
#include "BmpReader.h"
#include "TgaReader.h"
#include "PcxReader.h"

//=============================================================================
// Global Test Paths
//=============================================================================

#ifndef TEST_IMAGES_DIR
#define TEST_IMAGES_DIR "tests/images"
#endif

#ifndef TEST_REFERENCE_DIR
#define TEST_REFERENCE_DIR "tests/reference"
#endif

#ifndef TEST_OUTPUT_DIR
#define TEST_OUTPUT_DIR "tests/output"
#endif

const char* g_TestImagesDir = TEST_IMAGES_DIR;
const char* g_TestReferenceDir = TEST_REFERENCE_DIR;
const char* g_TestOutputDir = TEST_OUTPUT_DIR;

//=============================================================================
// Test Runner
//=============================================================================

namespace TestFramework {

class TestRunner {
public:
    TestRunner() : passed_(0), failed_(0), skipped_(0) {}

    int run(const std::string& filter = "") {
        const std::vector<TestCase>& tests = TestRegistry::instance().tests();

        printf("\n");
        printf("========================================\n");
        printf("  CKImageReader Test Suite\n");
        printf("========================================\n");
        printf("\n");
        printf("Test images: %s\n", g_TestImagesDir);
        printf("References:  %s\n", g_TestReferenceDir);
        printf("Output:      %s\n", g_TestOutputDir);
        printf("\n");

        if (tests.empty()) {
            printf("No tests registered!\n");
            return 1;
        }

        printf("Running %zu tests...\n\n", tests.size());

        std::vector<TestResult> results;
        results.reserve(tests.size());

        for (size_t i = 0; i < tests.size(); ++i) {
            const TestCase& tc = tests[i];

            // Filter
            if (!filter.empty()) {
                std::string fullName = tc.suite + "." + tc.name;
                if (fullName.find(filter) == std::string::npos) {
                    continue;
                }
            }

            TestResult result;
            result.name = tc.suite + "." + tc.name;
            result.status = TestStatus::Passed;

            // Set up test context
            TestRegistry::instance().currentResult = &result;
            TestRegistry::instance().currentFailed = false;

            clock_t start = clock();

            // Run test
            try {
                tc.func();
            } catch (const std::exception& e) {
                result.status = TestStatus::Failed;
                result.message = std::string("Exception: ") + e.what();
            } catch (...) {
                result.status = TestStatus::Failed;
                result.message = "Unknown exception";
            }

            clock_t end = clock();
            result.durationMs = 1000.0 * (end - start) / CLOCKS_PER_SEC;

            // Clear context
            TestRegistry::instance().currentResult = nullptr;

            // Report
            const char* statusStr = "?";
            const char* statusColor = "";
            switch (result.status) {
                case TestStatus::Passed:
                    statusStr = "PASS";
                    ++passed_;
                    break;
                case TestStatus::Failed:
                    statusStr = "FAIL";
                    ++failed_;
                    break;
                case TestStatus::Skipped:
                    statusStr = "SKIP";
                    ++skipped_;
                    break;
            }

            printf("[%s] %s (%.1f ms)\n", statusStr, result.name.c_str(), result.durationMs);
            if (!result.message.empty() && result.status != TestStatus::Passed) {
                printf("       %s\n", result.message.c_str());
            }

            results.push_back(result);
        }

        // Summary
        printf("\n");
        printf("========================================\n");
        printf("  Results: %d passed, %d failed, %d skipped\n", passed_, failed_, skipped_);
        printf("========================================\n");

        // List failures
        if (failed_ > 0) {
            printf("\nFailed tests:\n");
            for (size_t i = 0; i < results.size(); ++i) {
                if (results[i].status == TestStatus::Failed) {
                    printf("  - %s: %s\n", results[i].name.c_str(), results[i].message.c_str());
                }
            }
        }

        return (failed_ > 0) ? 1 : 0;
    }

private:
    int passed_;
    int failed_;
    int skipped_;
};

} // namespace TestFramework

//=============================================================================
// Reference CRC Generation Mode
//=============================================================================

#include <map>

// Global map to store generated CRCs - populated during --generate-refs mode
std::map<std::string, uint32_t> g_GeneratedCrcs;
bool g_GenerateRefsMode = false;

// Forward declarations for reader functions
struct ReaderTestResult {
    int errorCode;
    uint32_t crc;
    int width;
    int height;
};

template<typename ReaderType>
ReaderTestResult testReadFile(const std::string& path) {
    ReaderTestResult result = {-1, 0, 0, 0};
    ReaderType reader;
    CKBitmapProperties* props = nullptr;

    result.errorCode = reader.ReadFile(const_cast<char*>(path.c_str()), &props);

    if (result.errorCode == 0 && props) {
        result.width = props->m_Format.Width;
        result.height = props->m_Format.Height;
        int bytesPerLine = props->m_Format.BytesPerLine;

        if (props->m_Format.Image && result.height > 0 && bytesPerLine > 0) {
            size_t imageSize = static_cast<size_t>(bytesPerLine) * result.height;
            result.crc = TestFramework::CRC32::compute(props->m_Format.Image, imageSize);
        }

        ImageReader::FreeBitmapData(props);
    }

    return result;
}

void generateReferenceFile(const std::string& outputPath) {
    FILE* f = fopen(outputPath.c_str(), "w");
    if (!f) {
        printf("ERROR: Cannot create reference file: %s\n", outputPath.c_str());
        return;
    }

    fprintf(f, "# CKImageReader Reference CRCs\n");
    fprintf(f, "# Format: <filename>=<crc_hex>\n");
    fprintf(f, "# Generated automatically - do not edit manually\n\n");

    // BMP test images
    fprintf(f, "[bmp]\n");
    std::string bmpDir = TestFramework::joinPath(g_TestImagesDir, "bmp/images");
    std::vector<std::string> bmpFiles = TestFramework::listDirectory(bmpDir);
    for (size_t i = 0; i < bmpFiles.size(); ++i) {
        const std::string& file = bmpFiles[i];
        std::string ext = TestFramework::getExtension(file);
        std::string extLower = TestFramework::toLower(ext);
        if (extLower == ".bmp") {
            std::string path = TestFramework::joinPath(bmpDir, file);
            ReaderTestResult result = testReadFile<BmpReader>(path);
            if (result.errorCode == 0) {
                fprintf(f, "%s=%08x\n", file.c_str(), result.crc);
                g_GeneratedCrcs["bmp/" + file] = result.crc;
            }
        }
    }

    // TGA test images - testsuite
    fprintf(f, "\n[tga]\n");
    std::string tgaTestsuiteDir = TestFramework::joinPath(g_TestImagesDir, "tga/testsuite");
    std::vector<std::string> tgaTestsuiteFiles = TestFramework::listDirectory(tgaTestsuiteDir);
    for (size_t i = 0; i < tgaTestsuiteFiles.size(); ++i) {
        const std::string& file = tgaTestsuiteFiles[i];
        std::string ext = TestFramework::getExtension(file);
        std::string extLower = TestFramework::toLower(ext);
        if (extLower == ".tga") {
            std::string path = TestFramework::joinPath(tgaTestsuiteDir, file);
            ReaderTestResult result = testReadFile<TgaReader>(path);
            if (result.errorCode == 0) {
                fprintf(f, "testsuite/%s=%08x\n", file.c_str(), result.crc);
                g_GeneratedCrcs["tga/testsuite/" + file] = result.crc;
            }
        }
    }

    // TGA test images - encoding
    std::string tgaEncodingDir = TestFramework::joinPath(g_TestImagesDir, "tga/encoding");
    std::vector<std::string> tgaEncodingFiles = TestFramework::listDirectory(tgaEncodingDir);
    for (size_t i = 0; i < tgaEncodingFiles.size(); ++i) {
        const std::string& file = tgaEncodingFiles[i];
        std::string ext = TestFramework::getExtension(file);
        std::string extLower = TestFramework::toLower(ext);
        if (extLower == ".tga") {
            std::string path = TestFramework::joinPath(tgaEncodingDir, file);
            ReaderTestResult result = testReadFile<TgaReader>(path);
            if (result.errorCode == 0) {
                fprintf(f, "encoding/%s=%08x\n", file.c_str(), result.crc);
                g_GeneratedCrcs["tga/encoding/" + file] = result.crc;
            }
        }
    }

    // PCX test images
    fprintf(f, "\n[pcx]\n");
    std::string pcxDir = TestFramework::joinPath(g_TestImagesDir, "pcx");
    if (TestFramework::directoryExists(pcxDir)) {
        std::vector<std::string> pcxFiles = TestFramework::listDirectory(pcxDir);
        for (size_t i = 0; i < pcxFiles.size(); ++i) {
            const std::string& file = pcxFiles[i];
            std::string ext = TestFramework::getExtension(file);
            std::string extLower = TestFramework::toLower(ext);
            if (extLower == ".pcx") {
                std::string path = TestFramework::joinPath(pcxDir, file);
                ReaderTestResult result = testReadFile<PcxReader>(path);
                if (result.errorCode == 0) {
                    fprintf(f, "%s=%08x\n", file.c_str(), result.crc);
                    g_GeneratedCrcs["pcx/" + file] = result.crc;
                }
            }
        }
    }

    fclose(f);
    printf("Generated reference file: %s (%zu entries)\n", outputPath.c_str(), g_GeneratedCrcs.size());
}

std::map<std::string, uint32_t> loadReferenceCrcs(const std::string& path) {
    std::map<std::string, uint32_t> crcs;
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return crcs;

    char line[1024];
    std::string section;
    while (fgets(line, sizeof(line), f)) {
        // Strip newline
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = '\0';
        }

        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\0') continue;

        // Section header
        if (line[0] == '[') {
            char* end = strchr(line, ']');
            if (end) {
                *end = '\0';
                section = line + 1;
            }
            continue;
        }

        // Key=value
        char* eq = strchr(line, '=');
        if (eq) {
            *eq = '\0';
            std::string key = section + "/" + line;
            uint32_t crc = TestFramework::CRC32::fromHex(eq + 1);
            crcs[key] = crc;
        }
    }

    fclose(f);
    return crcs;
}

// Global reference CRCs loaded at startup
std::map<std::string, uint32_t> g_ReferenceCrcs;

bool getReferenceCrc(const std::string& key, uint32_t& outCrc) {
    std::map<std::string, uint32_t>::const_iterator it = g_ReferenceCrcs.find(key);
    if (it != g_ReferenceCrcs.end()) {
        outCrc = it->second;
        return true;
    }
    return false;
}

//=============================================================================
// Main
//=============================================================================

// Forward declaration of plugin initialization function
extern "C" PLUGIN_EXPORT CKPluginInfo* CKGetPluginInfo(int index);
extern "C" PLUGIN_EXPORT int CKGetPluginInfoCount();

int main(int argc, char* argv[]) {
    // Initialize plugin info (normally done by Virtools runtime when loading DLL)
    int pluginCount = CKGetPluginInfoCount();
    for (int i = 0; i < pluginCount; ++i) {
        CKGetPluginInfo(i);
    }

    // Check for --generate-refs mode
    bool generateRefs = false;
    std::string filter;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--generate-refs") == 0) {
            generateRefs = true;
        } else {
            filter = argv[i];
        }
    }

    // Generate reference file or load it
    std::string refFilePath = TestFramework::joinPath(g_TestReferenceDir, "ckimagereader_crcs.txt");
    if (generateRefs) {
        g_GenerateRefsMode = true;
        generateReferenceFile(refFilePath);
        printf("\nReference CRCs generated successfully.\n");
        printf("Run tests without --generate-refs to use these references.\n");
        return 0;
    }

    // Load reference CRCs
    g_ReferenceCrcs = loadReferenceCrcs(refFilePath);
    if (g_ReferenceCrcs.empty()) {
        printf("NOTE: No CKImageReader reference CRCs found.\n");
        printf("      Run with --generate-refs to generate them.\n\n");
    } else {
        printf("Loaded %zu reference CRCs\n", g_ReferenceCrcs.size());
    }

    if (!filter.empty()) {
        printf("Filter: %s\n", filter.c_str());
    }

    TestFramework::TestRunner runner;
    return runner.run(filter);
}
