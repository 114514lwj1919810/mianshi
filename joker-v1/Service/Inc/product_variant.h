#ifndef PRODUCT_VARIANT_H
#define PRODUCT_VARIANT_H

/*
 * 产品功能开关。
 *
 * 默认工程不定义 MACHINE_LOCK_FEATURE_ENABLE，因此编译为带打码版。
 * “无打码版”Keil 工程会在编译宏中把它定义为 0；这样两种固件共用
 * 同一套游戏、设置、查账和驱动源码，避免以后修复功能时两个目录失去同步。
 */
#ifndef MACHINE_LOCK_FEATURE_ENABLE
#define MACHINE_LOCK_FEATURE_ENABLE 1U
#endif

#endif
