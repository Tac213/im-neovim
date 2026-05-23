#include "im_app/font_manager.h"

#include <CoreText/CoreText.h>
#include <Foundation/Foundation.h>

#include <filesystem>
#include <string>

namespace ImApp {

// CoreText-based font discovery.
// Returns the file URL/path for a font matching the given family and traits.
static std::string find_font_coretext(const std::string &family_name, bool bold,
                                      bool italic) {
    @autoreleasepool {
        // Create a font descriptor with the family name
        NSString *ns_family =
            [NSString stringWithUTF8String:family_name.c_str()];

        // Build traits dictionary
        NSMutableDictionary *traits_dict = [NSMutableDictionary dictionary];
        if (bold) {
            traits_dict[(NSString *)kCTFontWeightTrait] =
                @(0.7); // approximate (kCTFontWeightBold equivalent)
        }
        if (italic) {
            traits_dict[(NSString *)kCTFontSlantTrait] =
                @(1.0); // kCTFontSlantItalic equivalent
        }

        NSDictionary *attributes = @{
            (NSString *)kCTFontFamilyNameAttribute : ns_family,
        };

        if (traits_dict.count > 0) {
            NSMutableDictionary *mutable_attrs = [attributes mutableCopy];
            mutable_attrs[(NSString *)kCTFontTraitsAttribute] = traits_dict;
            attributes = mutable_attrs;
        }

        CTFontDescriptorRef descriptor = CTFontDescriptorCreateWithAttributes(
            (__bridge CFDictionaryRef)attributes);
        if (!descriptor) {
            return "";
        }

        // Get matching descriptors
        CFArrayRef matches =
            CTFontDescriptorCreateMatchingFontDescriptors(descriptor, nullptr);
        if (!matches || CFArrayGetCount(matches) == 0) {
            if (descriptor)
                CFRelease(descriptor);
            if (matches)
                CFRelease(matches);
            return "";
        }

        // Get the first match's URL
        CTFontDescriptorRef matched =
            (CTFontDescriptorRef)CFArrayGetValueAtIndex(matches, 0);
        CFURLRef url = (CFURLRef)CTFontDescriptorCopyAttribute(
            matched, kCTFontURLAttribute);

        std::string result;
        if (url) {
            CFStringRef path_str =
                CFURLCopyFileSystemPath(url, kCFURLPOSIXPathStyle);
            if (path_str) {
                const char *cstr = [(__bridge NSString *)path_str UTF8String];
                if (cstr) {
                    std::string path(cstr);
                    if (std::filesystem::exists(path)) {
                        result = path;
                    }
                }
                CFRelease(path_str);
            }
            CFRelease(url);
        }

        CFRelease(matches);
        CFRelease(descriptor);

        return result;
    }
}

// Hardcoded fallback paths for Menlo
static std::string find_font_hardcoded(const std::string &family_name,
                                       bool bold, bool italic) {
    if (family_name != "Menlo") {
        return "";
    }

    // Menlo is shipped in a .ttc (TrueType Collection) on macOS
    // Face indices: 0=Regular, 1=Bold, 2=Italic, 3=Bold Italic
    std::filesystem::path font_path = "/System/Library/Fonts/Menlo.ttc";

    if (std::filesystem::exists(font_path)) {
        return font_path.string();
    }

    // Also check Supplemental folder (macOS 13+)
    font_path = "/System/Library/Fonts/Supplemental/Menlo.ttc";
    if (std::filesystem::exists(font_path)) {
        return font_path.string();
    }

    return "";
}

std::string FontManager::find_system_font(const std::string &family_name,
                                          bool bold, bool italic,
                                          bool /*allow_fallback*/) {
    // 1. Try CoreText
    std::string result = find_font_coretext(family_name, bold, italic);
    if (!result.empty()) {
        return result;
    }

    // 2. Try hardcoded paths
    result = find_font_hardcoded(family_name, bold, italic);
    if (!result.empty()) {
        return result;
    }

    return "";
}

} // namespace ImApp
