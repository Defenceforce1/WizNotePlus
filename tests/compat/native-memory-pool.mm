#import <Foundation/Foundation.h>
#include <functional>

// Isolated diagnostic control only. Do not change the application's normal
// event-loop lifetime or add browser-cache/data cleanup here.
int wizNativeMemoryBatchPool(const std::function<int()>& action)
{
    @autoreleasepool {
        return action();
    }
}
