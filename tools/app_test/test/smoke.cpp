#include <lvgl.h>
#include "Arduino.h"
#include "core/app_manager.hpp"
#include "core/lvgl_port.hpp"
#include "services/input_service.hpp"
#include "test_hooks.hpp"
#include <cstdio>
int main(){ LvglPort::init(); { LvglLock l; InputService::init(); AppManager::init(); }
 for(int i=0;i<50;i++){ hooks::nowMs+=10; AppManager::tick(hooks::nowMs); LvglPort::tick(); }
 printf("current=%d heap=%ld blocks=%ld flush=%ld\n",(int)AppManager::current(),hooks::heapBytes.load(),hooks::heapBlocks.load(),hooks::flushCount.load()); }
