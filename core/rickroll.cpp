/*
    Copyright 2024 Anthony Cruz

    This file is part of Flycast.

    Flycast is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Flycast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Flycast.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "rickroll.h"
#include "imgread/common.h"
#include "cfg/option.h"
#include "emulator.h"
#include "log/log.h"
#include "wsi/context.h"
#include <chrono>

// Platform-specific implementations
#if defined(__APPLE__) && !defined(TARGET_OS_OSX)
#include <UIKit/UIKit.h>
#include <WebKit/WebKit.h>
#endif

namespace rickroll {

bool isInitialized = false;
bool shouldRenderRickRoll = false;

#if defined(__APPLE__) && !defined(TARGET_OS_OSX)
// iOS-specific implementation
UIView* rickRollView = nullptr;
UIViewController* rickRollViewController = nullptr;
WKWebView* webView = nullptr;
#endif

bool init()
{
    if (isInitialized)
        return true;

    // Only initialize if we're in Rick Roll mode
    if (!gdr::rickRollMode)
        return false;

    INFO_LOG(COMMON, "Initializing Rick Roll player...");

#if defined(__APPLE__) && !defined(TARGET_OS_OSX)
    // iOS implementation
    dispatch_async(dispatch_get_main_queue(), ^{
        // Get the main window
        UIWindow* mainWindow = [UIApplication sharedApplication].keyWindow;
        if (!mainWindow) {
            INFO_LOG(COMMON, "Could not find main window for Rick Roll");
            return;
        }

        // Create a view controller for the video
        rickRollViewController = [[UIViewController alloc] init];
        rickRollView = [[UIView alloc] initWithFrame:mainWindow.bounds];
        rickRollViewController.view = rickRollView;

        // Create a video player with a web view
        NSURL* videoURL = [NSURL URLWithString:@"https://www.youtube.com/embed/dQw4w9WgXcQ?autoplay=1"];
        
        // Use WKWebView to load the video
        WKWebViewConfiguration *config = [[WKWebViewConfiguration alloc] init];
        config.allowsInlineMediaPlayback = YES;
        config.mediaTypesRequiringUserActionForPlayback = WKAudiovisualMediaTypeNone;
        
        webView = [[WKWebView alloc] initWithFrame:rickRollView.bounds configuration:config];
        [rickRollView addSubview:webView];
        
        NSURLRequest* request = [NSURLRequest requestWithURL:videoURL];
        [webView loadRequest:request];
        
        // Present the video player
        [mainWindow.rootViewController presentViewController:rickRollViewController animated:YES completion:nil];
    });
#else
    // Generic implementation for other platforms
    // For now, just log that we're in Rick Roll mode
    INFO_LOG(COMMON, "Rick Roll activated! (No platform-specific implementation yet)");
    
    // Mark that we should render Rick Roll video
    shouldRenderRickRoll = true;
#endif

    isInitialized = true;
    return true;
}

void term()
{
    if (!isInitialized)
        return;

#if defined(__APPLE__) && !defined(TARGET_OS_OSX)
    // iOS cleanup
    dispatch_async(dispatch_get_main_queue(), ^{
        if (rickRollViewController) {
            [rickRollViewController dismissViewControllerAnimated:YES completion:nil];
            rickRollViewController = nil;
            rickRollView = nil;
            webView = nil;
        }
    });
#endif

    isInitialized = false;
    shouldRenderRickRoll = false;
}

bool shouldRender()
{
    return gdr::rickRollMode && shouldRenderRickRoll;
}

void render()
{
    if (!shouldRender())
        return;

    // Platform-specific rendering would go here
    // For now, just a placeholder
}

void update()
{
    if (!gdr::rickRollMode)
        return;

    if (!isInitialized) {
        init();
    }
}

} // namespace rickroll 