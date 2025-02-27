/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 - 2020 Vivante Corporation
*
*    Permission is hereby granted, free of charge, to any person obtaining a
*    copy of this software and associated documentation files (the "Software"),
*    to deal in the Software without restriction, including without limitation
*    the rights to use, copy, modify, merge, publish, distribute, sublicense,
*    and/or sell copies of the Software, and to permit persons to whom the
*    Software is furnished to do so, subject to the following conditions:
*
*    The above copyright notice and this permission notice shall be included in
*    all copies or substantial portions of the Software.
*
*    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
*    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
*    DEALINGS IN THE SOFTWARE.
*
*****************************************************************************
*
*    The GPL License (GPL)
*
*    Copyright (C) 2014 - 2020 Vivante Corporation
*
*    This program is free software; you can redistribute it and/or
*    modify it under the terms of the GNU General Public License
*    as published by the Free Software Foundation; either version 2
*    of the License, or (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not, write to the Free Software Foundation,
*    Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*
*****************************************************************************
*
*    Note: This software is released under dual MIT and GPL licenses. A
*    recipient may use this file under the terms of either the MIT license or
*    GPL License. If you wish to use only one license not the other, you can
*    indicate your decision by deleting one of the above license notices in your
*    version of this file.
*
*****************************************************************************/


#include <linux/device.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>

#include "gc_hal_kernel_linux.h"
#include "gc_hal_driver.h"

#include <linux/of.h>
#include <linux/platform_device.h>

/* Zone used for header/footer. */
#define _GC_OBJ_ZONE    gcvZONE_DRIVER

MODULE_DESCRIPTION("Vivante Graphics Driver");
MODULE_LICENSE("Dual MIT/GPL");
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)
MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
#endif

static struct class* gpuClass = NULL;

static gcsPLATFORM *platform = NULL;

static gckGALDEVICE galDevice;

static uint major = 199;
module_param(major, uint, 0644);
MODULE_PARM_DESC(major, "major device number for GC device");

static int irqLine = -1;
module_param(irqLine, int, 0644);
MODULE_PARM_DESC(irqLine, "IRQ number of GC core");

static ulong registerMemBase = 0x80000000;
module_param(registerMemBase, ulong, 0644);
MODULE_PARM_DESC(registerMemBase, "Base of bus address of GC core AHB register");

static ulong registerMemSize = 2 << 16;
module_param(registerMemSize, ulong, 0644);
MODULE_PARM_DESC(registerMemSize, "Size of bus address range of GC core AHB register");

static int irqLine2D = -1;
module_param(irqLine2D, int, 0644);
MODULE_PARM_DESC(irqLine2D, "IRQ number of G2D core if irqLine is used for a G3D core");

static ulong registerMemBase2D = 0x00000000;
module_param(registerMemBase2D, ulong, 0644);
MODULE_PARM_DESC(registerMemBase2D, "Base of bus address of G2D core if registerMemBase2D is used for a G3D core");

static ulong registerMemSize2D = 2 << 16;
module_param(registerMemSize2D, ulong, 0644);
MODULE_PARM_DESC(registerMemSize2D, "Size of bus address range of G2D core if registerMemSize is used for a G3D core");

static int irqLineVG = -1;
module_param(irqLineVG, int, 0644);
MODULE_PARM_DESC(irqLineVG, "IRQ number of VG core");

static ulong registerMemBaseVG = 0x00000000;
module_param(registerMemBaseVG, ulong, 0644);
MODULE_PARM_DESC(registerMemBaseVG, "Base of bus address of VG core");

static ulong registerMemSizeVG = 2 << 10;
module_param(registerMemSizeVG, ulong, 0644);
MODULE_PARM_DESC(registerMemSizeVG, "Size of bus address range of VG core");

#if gcdDEC_ENABLE_AHB
static ulong registerMemBaseDEC300 = 0x00000000;
module_param(registerMemBaseDEC300, ulong, 0644);

static ulong registerMemSizeDEC300 = 2 << 10;
module_param(registerMemSizeDEC300, ulong, 0644);
#endif

#ifndef gcdDEFAULT_CONTIGUOUS_SIZE
#define gcdDEFAULT_CONTIGUOUS_SIZE (4 << 20)
#endif
static ulong contiguousSize = gcdDEFAULT_CONTIGUOUS_SIZE;
module_param(contiguousSize, ulong, 0644);
MODULE_PARM_DESC(contiguousSize, "Size of memory reserved for GC");

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
static gctPHYS_ADDR_T contiguousBase = 0;
module_param(contiguousBase, ullong, 0644);
#else
static ulong contiguousBase = 0;
module_param(contiguousBase, ulong, 0644);
#endif
MODULE_PARM_DESC(contiguousBase, "Base address of memory reserved for GC, if it is 0, GC driver will try to allocate a buffer whose size defined by contiguousSize");

static ulong externalSize = 0;
module_param(externalSize, ulong, 0644);
MODULE_PARM_DESC(externalSize, "Size of external memory, if it is 0, means there is no external pool");

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
static gctPHYS_ADDR_T externalBase = 0;
module_param(externalBase, ullong, 0644);
#else
static ulong externalBase = 0;
module_param(externalBase, ulong, 0644);
#endif
MODULE_PARM_DESC(externalBase, "Base address of external memory");


static int fastClear = -1;
module_param(fastClear, int, 0644);
MODULE_PARM_DESC(fastClear, "Disable fast clear if set it to 0, enabled by default");

static int compression = -1;
module_param(compression, int, 0644);
MODULE_PARM_DESC(compression, "Disable compression if set it to 0, enabled by default");

static int powerManagement = 1;
module_param(powerManagement, int, 0644);
MODULE_PARM_DESC(powerManagement, "Disable auto power saving if set it to 0, enabled by default");

static ulong baseAddress = 0;
module_param(baseAddress, ulong, 0644);
MODULE_PARM_DESC(baseAddress, "Only used for old MMU, set it to 0 if memory which can be accessed by GPU falls into 0 - 2G, otherwise set it to 0x80000000");

static ulong physSize = 0;
module_param(physSize, ulong, 0644);
MODULE_PARM_DESC(physSize, "Obsolete");

static uint recovery = 0;
module_param(recovery, uint, 0644);
MODULE_PARM_DESC(recovery, "Recover GPU from stuck (1: Enable, 0: Disable)");

/*
 * Level of stuck dump content, 0~5 and 11~15.
 * 0: Disable. 1: Dump nearby memory. 2: Dump user command.
 * 3: Commit stall besides level2. 4: Dump kernel command buffer besides level3.
 * 5: Dump all the cores with level4.
 *
 * Level 1~5 + 10 means force dump, whether the recovery is enabled or not.
 */
static uint stuckDump = 0;
module_param(stuckDump, uint, 0644);
MODULE_PARM_DESC(stuckDump, "Level of stuck dump content.");

static int showArgs = 0;
module_param(showArgs, int, 0644);
MODULE_PARM_DESC(showArgs, "Display parameters value when driver loaded");

static int mmu = 1;
module_param(mmu, int, 0644);
MODULE_PARM_DESC(mmu, "Disable MMU if set it to 0, enabled by default [Obsolete]");

static uint mmuException = 1;
module_param(mmuException, uint, 0644);
MODULE_PARM_DESC(mmuException, "use MMU Exception (1: Enable, 0: Disable)");

static int irqs[gcvCORE_COUNT] = {[0 ... gcvCORE_COUNT - 1] = -1};
module_param_array(irqs, int, NULL, 0644);
MODULE_PARM_DESC(irqs, "Array of IRQ numbers of multi-GPU");

static uint registerBases[gcvCORE_COUNT];
module_param_array(registerBases, uint, NULL, 0644);
MODULE_PARM_DESC(registerBases, "Array of bases of bus address of register of multi-GPU");

static uint registerSizes[gcvCORE_COUNT] = {[0 ... gcvCORE_COUNT - 1] = 2 << 16};
module_param_array(registerSizes, uint, NULL, 0644);
MODULE_PARM_DESC(registerSizes, "Array of sizes of bus address range of register of multi-GPU");

static uint chipIDs[gcvCORE_COUNT] = {[0 ... gcvCORE_COUNT - 1] = gcvCHIP_ID_DEFAULT};
module_param_array(chipIDs, uint, NULL, 0644);
MODULE_PARM_DESC(chipIDs, "Array of chipIDs of multi-GPU");

static uint type = 0;
module_param(type, uint, 0664);
MODULE_PARM_DESC(type, "0 - Char Driver (Default), 1 - Misc Driver");

static int userClusterMask = 0;
module_param(userClusterMask, int, 0644);
MODULE_PARM_DESC(userClusterMask, "User defined cluster enable mask");

/* GPU small batch feature. */
static int smallBatch = 1;
module_param(smallBatch, int, 0644);
MODULE_PARM_DESC(smallBatch, "Enable/disable GPU small batch feature, enable by default");

static int allMapInOne = 1;
module_param(allMapInOne, int, 0644);
MODULE_PARM_DESC(allMapInOne, "Mapping kernel video memory to user, 0 means mapping every time, otherwise only mapping one time");
/*******************************************************************************
***************************** SRAM description *********************************
*******************************************************************************/

/* Per-core SRAM physical address base, the order of configuration is according to access speed, gcvINVALID_PHYSICAL_ADDRESS means no bus address. */
static gctPHYS_ADDR_T sRAMBases[gcvSRAM_INTER_COUNT * gcvCORE_COUNT] = {[0 ... gcvSRAM_INTER_COUNT * gcvCORE_COUNT - 1] = gcvINVALID_PHYSICAL_ADDRESS};
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
module_param_array(sRAMBases, ullong, NULL, 0644);
MODULE_PARM_DESC(sRAMBases, "Array of base of bus address of SRAM,INTERNAL, EXTERNAL0, EXTERNAL1..., gcvINVALID_PHYSICAL_ADDRESS means no bus address");
#endif

/* Per-core SRAM size. */
static uint sRAMSizes[gcvSRAM_INTER_COUNT * gcvCORE_COUNT] = {[0 ... gcvSRAM_INTER_COUNT * gcvCORE_COUNT - 1] = 0};
module_param_array(sRAMSizes, uint, NULL, 0644);
MODULE_PARM_DESC(sRAMSizes, "Array of size of per-core SRAMs, 0 means no SRAM");

/* Shared SRAM physical address bases. */
static gctPHYS_ADDR_T extSRAMBases[gcvSRAM_EXT_COUNT] = {[0 ... gcvSRAM_EXT_COUNT - 1] = gcvINVALID_PHYSICAL_ADDRESS};
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
module_param_array(extSRAMBases, ullong, NULL, 0644);
MODULE_PARM_DESC(extSRAMBases, "Shared SRAM physical address bases.");
#endif

/* Shared SRAM sizes. */
static uint extSRAMSizes[gcvSRAM_EXT_COUNT] = {[0 ... gcvSRAM_EXT_COUNT - 1] = 0};
module_param_array(extSRAMSizes, uint, NULL, 0644);
MODULE_PARM_DESC(extSRAMSizes, "Shared SRAM sizes.");

static uint sRAMRequested = 1;
module_param(sRAMRequested, uint, 0644);
MODULE_PARM_DESC(sRAMRequested, "Default 1 means AXI-SRAM is already reserved for GPU, 0 means GPU driver need request the memory region.");

static uint mmuPageTablePool = 1;
module_param(mmuPageTablePool, uint, 0644);
MODULE_PARM_DESC(mmuPageTablePool, "Default 1 means alloc mmu page table in virsual memory, 0 means auto select memory pool.");

static uint sRAMLoopMode = 0;
module_param(sRAMLoopMode, uint, 0644);
MODULE_PARM_DESC(sRAMLoopMode, "Default 0 means SRAM pool must be specified when allocating SRAM memory, 1 means SRAM memory will be looped as default pool.");

static uint mmuDynamicMap = 1;
module_param(mmuDynamicMap, uint, 0644);
MODULE_PARM_DESC(mmuDynamicMap, "Default 1 means enable mmu dynamic mapping in virsual memory, 0 means disable dynnamic mapping.");

#if USE_LINUX_PCIE
static int bar = 1;
module_param(bar, int, 0644);
MODULE_PARM_DESC(bar, "PCIE Bar index of GC core");

static int bars[gcvCORE_COUNT] = {[0 ... gcvCORE_COUNT - 1] = -1};
module_param_array(bars, int, NULL, 0644);
MODULE_PARM_DESC(bars, "Array of bar index of PCIE platform for multi-GPU");

static uint regOffsets[gcvCORE_COUNT] = {[0 ... gcvCORE_COUNT - 1] = 0};
module_param_array(regOffsets, uint, NULL, 0644);
MODULE_PARM_DESC(regOffsets, "Array of register offsets in corresponding BAR space");

static int sRAMBars[gcvSRAM_EXT_COUNT] = {[0 ... gcvSRAM_EXT_COUNT - 1] = -1};
module_param_array(sRAMBars, int, NULL, 0644);
MODULE_PARM_DESC(sRAMBars, "Array of SRAM bar index of shared external SRAMs.");

static int sRAMOffsets[gcvSRAM_EXT_COUNT] = {[0 ... gcvSRAM_EXT_COUNT - 1] = -1};
module_param_array(sRAMOffsets, int, NULL, 0644);
MODULE_PARM_DESC(sRAMOffsets, "Array of SRAM offset inside bar of shared external SRAMs.");
#endif

static int gpu3DMinClock = 1;
static int contiguousRequested = 0;
static ulong bankSize = 0;


static gcsMODULE_PARAMETERS moduleParam;

static void
_InitModuleParam(
    gcsMODULE_PARAMETERS * ModuleParam
    )
{
    gctUINT i, j;
    gcsMODULE_PARAMETERS *p = ModuleParam;

    for (i = 0; i < gcvCORE_COUNT; i++)
    {
        p->irqs[i] = irqs[i];

        if (irqs[i] != -1)
        {
            p->registerBases[i] = registerBases[i];
            p->registerSizes[i] = registerSizes[i];
        }
#if USE_LINUX_PCIE
        p->bars[i] = bars[i];
        p->regOffsets[i] = regOffsets[i];
#endif
    }

    /* Check legacy style. */
#if USE_LINUX_PCIE
    if (bar != -1)
    {
        p->bars[gcvCORE_MAJOR]         = bar;
    }
#endif

    if (irqLine != -1)
    {
        p->irqs[gcvCORE_MAJOR]          = irqLine;
        p->registerBases[gcvCORE_MAJOR] = registerMemBase;
        p->registerSizes[gcvCORE_MAJOR] = registerMemSize;
    }

    if (irqLine2D != -1)
    {
        p->irqs[gcvCORE_2D]          = irqLine2D;
        p->registerBases[gcvCORE_2D] = registerMemBase2D;
        p->registerSizes[gcvCORE_2D] = registerMemSize2D;
    }

    if (irqLineVG != -1)
    {
        p->irqs[gcvCORE_VG]          = irqLineVG;
        p->registerBases[gcvCORE_VG] = registerMemBaseVG;
        p->registerSizes[gcvCORE_VG] = registerMemSizeVG;
    }

#if gcdDEC_ENABLE_AHB
    if (registerMemBaseDEC300 && registerMemSizeDEC300)
    {
        p->registerBases[gcvCORE_DEC] = registerMemBaseDEC300;
        p->registerSizes[gcvCORE_DEC] = registerMemSizeDEC300;
    }
#endif

    for (i = 0; i < gcvCORE_COUNT; i++)
    {
        /* Not a module param. */
        p->registerBasesMapped[i] = gcvNULL;
    }

    for (i = 0; i < gcvCORE_COUNT; i++)
    {
        p->chipIDs[i] = chipIDs[i];
    }

    p->contiguousBase      = contiguousBase;
    p->contiguousSize      = contiguousSize;
    p->contiguousRequested = contiguousRequested;   /* not a module param. */

    p->externalBase = externalBase;
    p->externalSize = externalSize;

    for (i = 0; i < gcvCORE_COUNT; i++)
    {
        for (j = 0; j < gcvSRAM_INTER_COUNT; j++)
        {
            p->sRAMBases[i][j] = sRAMBases[i * gcvSRAM_INTER_COUNT + j];
            p->sRAMSizes[i][j] = sRAMSizes[i * gcvSRAM_INTER_COUNT + j];
        }
    }

    for (i = 0; i < gcvSRAM_EXT_COUNT; i++)
    {
        p->extSRAMBases[i] = extSRAMBases[i];
        p->extSRAMSizes[i] = extSRAMSizes[i];
#if USE_LINUX_PCIE
        p->sRAMBars[i]     = sRAMBars[i];
        p->sRAMOffsets[i]  = sRAMOffsets[i];
#endif
    }

    p->sRAMRequested = sRAMRequested;
    p->sRAMLoopMode = sRAMLoopMode;

    p->baseAddress = baseAddress;
    p->physSize    = physSize;
    p->bankSize    = bankSize;  /* not a module param. */

    p->recovery        = recovery;
    p->powerManagement = powerManagement;

    p->enableMmu = mmu;
    p->mmuException = mmuException;
    p->fastClear = fastClear;

    p->compression = (compression == -1) ? gcvCOMPRESSION_OPTION_DEFAULT
                   : (gceCOMPRESSION_OPTION)compression;
    p->gpu3DMinClock   = gpu3DMinClock; /* not a module param. */
    p->userClusterMask = userClusterMask;
    p->smallBatch      = smallBatch;

    p->stuckDump   = stuckDump;

    p->deviceType  = type;
    p->showArgs    = showArgs;

    p->mmuPageTablePool = mmuPageTablePool;

    p->mmuDynamicMap = mmuDynamicMap;
    p->allMapInOne = allMapInOne;
#if !gcdENABLE_3D
    p->irqs[gcvCORE_MAJOR]          = irqLine = -1;
    p->registerBases[gcvCORE_MAJOR] = registerMemBase = 0;
    p->registerSizes[gcvCORE_MAJOR] = registerMemSize = 0;
#endif

#if !gcdENABLE_2D
    p->irqs[gcvCORE_2D]          = irqLine2D = -1;
    p->registerBases[gcvCORE_2D] = registerMemBase2D = 0;
    p->registerSizes[gcvCORE_2D] = registerMemSize2D = 0;
#endif

#if !gcdENABLE_VG
    p->irqs[gcvCORE_VG]          = irqLineVG = -1;
    p->registerBases[gcvCORE_VG] = registerMemBaseVG = 0;
    p->registerSizes[gcvCORE_VG] = registerMemSizeVG = 0;
#endif
}

static void
_SyncModuleParam(
    const gcsMODULE_PARAMETERS * ModuleParam
    )
{
    gctUINT i, j;
    gcsMODULE_PARAMETERS *p = &moduleParam;

    for (i = 0; i < gcvCORE_COUNT; i++)
    {
        irqs[i]          = p->irqs[i];
        registerBases[i] = (ulong)p->registerBases[i];
        registerSizes[i] = (ulong)p->registerSizes[i];
 #if USE_LINUX_PCIE
        bars[i]          = p->bars[i];
        regOffsets[i]    = p->regOffsets[i];
 #endif
    }

    /* Sync to legacy style. */

#if USE_LINUX_PCIE
    bar               = p->bars[gcvCORE_MAJOR];
#endif
    irqLine           = p->irqs[gcvCORE_MAJOR];
    registerMemBase   = (ulong)p->registerBases[gcvCORE_MAJOR];
    registerMemSize   = (ulong)p->registerSizes[gcvCORE_MAJOR];

    irqLine2D         = p->irqs[gcvCORE_2D];
    registerMemBase2D = (ulong)p->registerBases[gcvCORE_2D];
    registerMemSize2D = (ulong)p->registerSizes[gcvCORE_2D];

    irqLineVG         = p->irqs[gcvCORE_VG];
    registerMemBaseVG = (ulong)p->registerBases[gcvCORE_VG];
    registerMemSizeVG = (ulong)p->registerSizes[gcvCORE_VG];

    for (i = 0; i < gcvCORE_COUNT; i++)
    {
        p->chipIDs[i] = chipIDs[i];
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
    contiguousBase      = p->contiguousBase;
    contiguousSize      = p->contiguousSize;
#else
    contiguousBase      = (ulong)p->contiguousBase;
    contiguousSize      = (ulong)p->contiguousSize;
#endif

    contiguousRequested = p->contiguousRequested;   /* not a module param. */

    externalBase = p->externalBase;
    externalSize = p->externalSize;

    for (i = 0; i < gcvCORE_COUNT; i++)
    {
        for (j = 0; j < gcvSRAM_INTER_COUNT; j++)
        {
            sRAMBases[i * gcvSRAM_INTER_COUNT + j] = p->sRAMBases[i][j];
            sRAMSizes[i * gcvSRAM_INTER_COUNT + j] = p->sRAMSizes[i][j];
        }
    }

    for (i = 0; i < gcvSRAM_EXT_COUNT; i++)
    {
        extSRAMBases[i] = p->extSRAMBases[i];
        extSRAMSizes[i] = p->extSRAMSizes[i];

#if USE_LINUX_PCIE
        sRAMBars[i]     = p->sRAMBars[i];
        sRAMOffsets[i]  = p->sRAMOffsets[i];
#endif
    }

    sRAMRequested = p->sRAMRequested;
    sRAMLoopMode  = p->sRAMLoopMode;

    baseAddress = (ulong)p->baseAddress;
    physSize    = p->physSize;
    bankSize    = p->bankSize;  /* not a module param. */

    recovery        = p->recovery;
    powerManagement = p->powerManagement;

    mmu             = p->enableMmu;
    mmuException    = p->mmuException;
    fastClear       = p->fastClear;
    compression     = p->compression;
    gpu3DMinClock   = p->gpu3DMinClock; /* not a module param. */
    userClusterMask = p->userClusterMask;
    smallBatch      = p->smallBatch;

    stuckDump   = p->stuckDump;

    type        = p->deviceType;
    showArgs    = p->showArgs;

    mmuPageTablePool = p->mmuDynamicMap;
    mmuDynamicMap = p->mmuDynamicMap;
    allMapInOne = p->allMapInOne;
}

void
gckOS_DumpParam(
    void
    )
{
    gctINT i, j;

    printk("Galcore options:\n");
    if (irqLine != -1)
    {
        printk("  irqLine           = %d\n",      irqLine);
        printk("  registerMemBase   = 0x%08lX\n", registerMemBase);
        printk("  registerMemSize   = 0x%08lX\n", registerMemSize);
    }

    if (irqLine2D != -1)
    {
        printk("  irqLine2D         = %d\n",      irqLine2D);
        printk("  registerMemBase2D = 0x%08lX\n", registerMemBase2D);
        printk("  registerMemSize2D = 0x%08lX\n", registerMemSize2D);
    }

    if (irqLineVG != -1)
    {
        printk("  irqLineVG         = %d\n",      irqLineVG);
        printk("  registerMemBaseVG = 0x%08lX\n", registerMemBaseVG);
        printk("  registerMemSizeVG = 0x%08lX\n", registerMemSizeVG);
    }
#if USE_LINUX_PCIE
    if (bar != -1)
    {
        printk("  bar               = %d\n",      bar);
    }
#endif
#if gcdDEC_ENABLE_AHB
    printk("  registerMemBaseDEC300 = 0x%08lX\n", registerMemBaseDEC300);
    printk("  registerMemSizeDEC300 = 0x%08lX\n", registerMemSizeDEC300);
#endif

    printk("  contiguousSize    = 0x%08lX\n", contiguousSize);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
    printk("  contiguousBase    = 0x%llX\n",  contiguousBase);
#else
    printk("  contiguousBase    = 0x%lX\n",  contiguousBase);
#endif
    printk("  externalSize      = 0x%08lX\n", externalSize);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
    printk("  externalBase      = 0x%llX\n",  externalBase);
#else
    printk("  externalBase      = 0x%lX\n",  externalBase);
#endif
    printk("  bankSize          = 0x%08lX\n", bankSize);
    printk("  fastClear         = %d\n",      fastClear);
    printk("  compression       = %d\n",      compression);
    printk("  powerManagement   = %d\n",      powerManagement);
    printk("  baseAddress       = 0x%08lX\n", baseAddress);
    printk("  physSize          = 0x%08lX\n", physSize);
    printk("  recovery          = %d\n",      recovery);
    printk("  stuckDump         = %d\n",      stuckDump);
    printk("  userClusterMask   = 0x%x\n",    userClusterMask);
    printk("  GPU smallBatch    = %d\n",      smallBatch);
    printk("  allMapInOne       = %d\n",      allMapInOne);
    printk("  mmuException      = %d\n",      mmuException);

    printk("  irqs              = ");
    for (i = 0; i < gcvCORE_COUNT; i++)
    {
        printk("%d, ", irqs[i]);
    }
    printk("\n");

#if USE_LINUX_PCIE
    printk("  bars              = ");
    for (i = 0; i < gcvCORE_COUNT; i++)
    {
        printk("%d, ", bars[i]);
    }
    printk("\n");

    printk("  regOffsets        = ");
    for (i = 0; i < gcvCORE_COUNT; i++)
    {
        printk("%d, ", regOffsets[i]);
    }
    printk("\n");

#endif
    printk("  registerBases     = ");
    for (i = 0; i < gcvCORE_COUNT; i++)
    {
        printk("0x%08X, ", registerBases[i]);
    }
    printk("\n");

    printk("  registerSizes     = ");
    for (i = 0; i < gcvCORE_COUNT; i++)
    {
        printk("0x%08X, ", registerSizes[i]);
    }
    printk("\n");

    printk("  chipIDs           = ");
    for (i = 0; i < gcvCORE_COUNT; i++)
    {
        printk("0x%08X, ", chipIDs[i]);
    }
    printk("\n");

    for (i = 0; i < gcvCORE_COUNT; i++)
    {
        printk("  core %d internal sRAMBases = ", i);

        for (j = 0; j < gcvSRAM_INTER_COUNT; j++)
        {
            printk("0x%llX, ", sRAMBases[i * gcvSRAM_INTER_COUNT + j]);
        }
        printk("\n");
    }

    printk("  External sRAMBases = ");
    for (i = 0; i < gcvSRAM_EXT_COUNT; i++)
    {
        printk("0x%llx, ", extSRAMBases[i]);
    }
    printk("\n");

    printk("  mmuPageTablePool  = %d\n", mmuPageTablePool);
    printk("  mmuDynamicMap     = %d\n", mmuDynamicMap);

    printk("Build options:\n");
    printk("  gcdGPU_TIMEOUT    = %d\n", gcdGPU_TIMEOUT);
    printk("  gcdGPU_2D_TIMEOUT = %d\n", gcdGPU_2D_TIMEOUT);
    printk("  gcdINTERRUPT_STATISTIC = %d\n", gcdINTERRUPT_STATISTIC);
}

static int drv_open(
    struct inode* inode,
    struct file* filp
    )
{
    gceSTATUS status = gcvSTATUS_OK;
    gcsHAL_PRIVATE_DATA_PTR data = gcvNULL;
    gctINT i;
    gctINT attached = 0;

    gcmkHEADER_ARG("inode=%p filp=%p", inode, filp);

    data = kmalloc(sizeof(gcsHAL_PRIVATE_DATA), GFP_KERNEL | __GFP_NOWARN);

    if (data == gcvNULL)
    {
        gcmkFOOTER_ARG("status=%d", gcvSTATUS_OUT_OF_MEMORY);
        return -ENOMEM;
    }

    data->isLocked = gcvFALSE;
    data->device   = galDevice;
    data->pidOpen  = _GetProcessID();

    /* Attached the process. */
    for (i = 0; i < gcdMAX_GPU_COUNT; i++)
    {
        if (galDevice->kernels[i] != gcvNULL)
        {
            status = gckKERNEL_AttachProcess(galDevice->kernels[i], gcvTRUE);

            if (gcmIS_ERROR(status))
            {
                break;
            }

            attached = i;
        }
    }

    if (gcmIS_ERROR(status))
    {
        /* Error. */
        for (i = 0; i < attached; i++)
        {
            if (galDevice->kernels[i] != gcvNULL)
            {
                gcmkVERIFY_OK(gckKERNEL_AttachProcess(galDevice->kernels[i], gcvFALSE));
            }
        }
        kfree(data);
        gcmkFOOTER_ARG("status=%d", status);
        return -ENOTTY;
    }

    filp->private_data = data;

    /* Success. */
    gcmkFOOTER_NO();
    return 0;
}

static int drv_release(
    struct inode* inode,
    struct file* filp
    )
{
    int ret = -ENOTTY;
    gceSTATUS status = gcvSTATUS_OK;
    gcsHAL_PRIVATE_DATA_PTR data;
    gckGALDEVICE device;
    gctINT i;

    gcmkHEADER_ARG("inode=%p filp=%p", inode, filp);

    data = filp->private_data;

    if (data == gcvNULL)
    {
        gcmkTRACE_ZONE(
            gcvLEVEL_ERROR, gcvZONE_DRIVER,
            "%s(%d): private_data is NULL\n",
            __FUNCTION__, __LINE__
            );

        gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
    }

    device = data->device;

    if (device == gcvNULL)
    {
        gcmkTRACE_ZONE(
            gcvLEVEL_ERROR, gcvZONE_DRIVER,
            "%s(%d): device is NULL\n",
            __FUNCTION__, __LINE__
            );

        gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
    }

    if (data->isLocked)
    {
        /* Release the mutex. */
        gcmkONERROR(gckOS_ReleaseMutex(gcvNULL, device->device->commitMutex));
        data->isLocked = gcvFALSE;
    }

    /* A process gets detached. */
    for (i = 0; i < gcdMAX_GPU_COUNT; i++)
    {
        if (galDevice->kernels[i] != gcvNULL)
        {
            gcmkVERIFY_OK(gckKERNEL_AttachProcessEx(
                galDevice->kernels[i],
                gcvFALSE,
                data->pidOpen
                ));
        }
    }

    kfree(data);
    filp->private_data = NULL;

    /* Success. */
    ret = 0;

OnError:
    gcmkFOOTER();
    return ret;
}

static long drv_ioctl(
    struct file* filp,
    unsigned int ioctlCode,
    unsigned long arg
    )
{
    long ret = -ENOTTY;
    gceSTATUS status = gcvSTATUS_OK;
    gcsHAL_INTERFACE iface;

#if VIVANTE_PROFILER
    gcsHAL_PROFILER_INTERFACE iface_profiler;
#endif

    gctUINT32 copyLen;
    DRIVER_ARGS drvArgs;
    gckGALDEVICE device;
    gcsHAL_PRIVATE_DATA_PTR data;

    gcmkHEADER_ARG("filp=%p ioctlCode=%u arg=%lu",filp, ioctlCode, arg);

    data = filp->private_data;

    if (data == gcvNULL)
    {
        gcmkTRACE_ZONE(
            gcvLEVEL_ERROR, gcvZONE_DRIVER,
            "%s(%d): private_data is NULL\n",
            __FUNCTION__, __LINE__
            );

        gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
    }

    device = data->device;

    if (device == gcvNULL)
    {
        gcmkTRACE_ZONE(
            gcvLEVEL_ERROR, gcvZONE_DRIVER,
            "%s(%d): device is NULL\n",
            __FUNCTION__, __LINE__
            );

        gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
    }

    switch (ioctlCode)
    {
    case IOCTL_GCHAL_INTERFACE:
        /* Get the drvArgs. */
        copyLen = copy_from_user(
            &drvArgs, (void *) arg, sizeof(DRIVER_ARGS)
            );

        if (copyLen != 0)
        {
            gcmkTRACE_ZONE(
                gcvLEVEL_ERROR, gcvZONE_DRIVER,
                "%s(%d): error copying of the input arguments.\n",
                __FUNCTION__, __LINE__
                );

            gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
        }

        /* Now bring in the gcsHAL_INTERFACE structure. */
        if ((drvArgs.InputBufferSize  != sizeof(gcsHAL_INTERFACE))
        ||  (drvArgs.OutputBufferSize != sizeof(gcsHAL_INTERFACE))
        )
        {
            gcmkTRACE_ZONE(
                gcvLEVEL_ERROR, gcvZONE_DRIVER,
                "%s(%d): input or/and output structures are invalid.\n",
                __FUNCTION__, __LINE__
                );

            gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
        }

        copyLen = copy_from_user(
            &iface, gcmUINT64_TO_PTR(drvArgs.InputBuffer), sizeof(gcsHAL_INTERFACE)
            );

        if (copyLen != 0)
        {
            gcmkTRACE_ZONE(
                gcvLEVEL_ERROR, gcvZONE_DRIVER,
                "%s(%d): error copying of input HAL interface.\n",
                __FUNCTION__, __LINE__
                );

            gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
        }

        if (iface.command == gcvHAL_DEVICE_MUTEX)
        {
            if (iface.u.DeviceMutex.isMutexLocked == gcvTRUE)
            {
                data->isLocked = gcvTRUE;
            }
            else
            {
                data->isLocked = gcvFALSE;
            }
        }

        status = gckDEVICE_Dispatch(device->device, &iface);

        /* Redo system call after pending signal is handled. */
        if (status == gcvSTATUS_INTERRUPTED)
        {
            ret = -ERESTARTSYS;
            gcmkONERROR(status);
        }

        /* Copy data back to the user. */
        copyLen = copy_to_user(
            gcmUINT64_TO_PTR(drvArgs.OutputBuffer), &iface, sizeof(gcsHAL_INTERFACE)
            );

        if (copyLen != 0)
        {
            gcmkTRACE_ZONE(
                gcvLEVEL_ERROR, gcvZONE_DRIVER,
                "%s(%d): error copying of output HAL interface.\n",
                __FUNCTION__, __LINE__
                );

            gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
        }
        break;

    case IOCTL_GCHAL_PROFILER_INTERFACE:
#if VIVANTE_PROFILER
        /* Get the drvArgs. */
        copyLen = copy_from_user(
            &drvArgs, (void *) arg, sizeof(DRIVER_ARGS)
            );

        if (copyLen != 0)
        {
           gcmkTRACE_ZONE(
                gcvLEVEL_ERROR, gcvZONE_DRIVER,
                "%s(%d): error copying of the input arguments.\n",
                __FUNCTION__, __LINE__
                );

            gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
        }

        /* Now bring in the gcsHAL_INTERFACE structure. */
        if ((drvArgs.InputBufferSize  != sizeof(gcsHAL_PROFILER_INTERFACE))
        ||  (drvArgs.OutputBufferSize != sizeof(gcsHAL_PROFILER_INTERFACE))
        )
        {
            gcmkTRACE_ZONE(
                gcvLEVEL_ERROR, gcvZONE_DRIVER,
                "%s(%d): input or/and output structures are invalid.\n",
                __FUNCTION__, __LINE__
                );

            gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
        }

        copyLen = copy_from_user(
            &iface_profiler, gcmUINT64_TO_PTR(drvArgs.InputBuffer), sizeof(gcsHAL_PROFILER_INTERFACE)
            );

        if (copyLen != 0)
        {
            gcmkTRACE_ZONE(
                gcvLEVEL_ERROR, gcvZONE_DRIVER,
                "%s(%d): error copying of input HAL interface.\n",
                __FUNCTION__, __LINE__
                );

            gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
        }

        status = gckDEVICE_Profiler_Dispatch(device->device, &iface_profiler);

        /* Redo system call after pending signal is handled. */
        if (status == gcvSTATUS_INTERRUPTED)
        {
            ret = -ERESTARTSYS;
            gcmkONERROR(status);
        }

        /* Copy data back to the user. */
        copyLen = copy_to_user(
            gcmUINT64_TO_PTR(drvArgs.OutputBuffer), &iface_profiler, sizeof(gcsHAL_PROFILER_INTERFACE)
            );

        if (copyLen != 0)
        {
            gcmkTRACE_ZONE(
                gcvLEVEL_ERROR, gcvZONE_DRIVER,
                "%s(%d): error copying of output HAL interface.\n",
                __FUNCTION__, __LINE__
                );

            gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
        }
#endif
        break;

    default:
        gcmkTRACE_ZONE(
            gcvLEVEL_ERROR, gcvZONE_DRIVER,
            "%s(%d): unknown command %d\n",
            __FUNCTION__, __LINE__,
            ioctlCode
            );

        gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
    }

    /* Success. */
    ret = 0;

OnError:
    gcmkFOOTER();
    return ret;
}

static ssize_t gpu_mult_show(struct device *dev, struct device_attribute *attr,
                             char *buf)
{
    static gctUINT gpu_clk_mult, min_clk_mult, max_clk_mult;
    gckHARDWARE hardware;
    gckGALDEVICE galDevice;

    galDevice = dev_get_drvdata(dev);
    if (!(galDevice) ||
        !(galDevice->kernels[gcvCORE_MAJOR])) {
        /* GPU is not ready, so it is meaningless to change GPU freq. */
        dev_err(dev, "galDevice or galDevice->kernels is NULL!\n");
        return NOTIFY_OK;
    }

    hardware = galDevice->kernels[gcvCORE_MAJOR]->hardware;
    if (!hardware) {
        dev_err(dev, "hardware is NULL!\n");
        return NOTIFY_OK;
    }

    gckHARDWARE_GetFscaleValue(hardware, &gpu_clk_mult, &min_clk_mult,
                               &max_clk_mult);

    return sprintf(buf, "%d\n", gpu_clk_mult);
}

static ssize_t gpu_mult_store(struct device *dev,
                              struct device_attribute *attr,
                              const char *buf, size_t count)
{
    gckHARDWARE hardware;
    gckGALDEVICE galDevice;
    int err;
    unsigned long gpu_clk_mult;

    galDevice = dev_get_drvdata(dev);
    if (!(galDevice) ||
        !(galDevice->kernels[gcvCORE_MAJOR])) {
        /* GPU is not ready, so it is meaningless to change GPU freq. */
        dev_err(dev, "galDevice or galDevice->kernels is NULL!\n");
        return NOTIFY_OK;
    }

    hardware = galDevice->kernels[gcvCORE_MAJOR]->hardware;
    if (!hardware) {
        dev_err(dev, "hardware is NULL!\n");
        return NOTIFY_OK;
    }

    err = kstrtoul(buf, 0, &gpu_clk_mult);

    if (of_machine_is_compatible("fsl,imx6dl")) {
        if (gpu_clk_mult < 3 || gpu_clk_mult > 64) {
            dev_err(dev, "gpu clock multiplier must be between 3 and 64\n");
            return NOTIFY_OK;
        }
    } else if (of_machine_is_compatible("fsl,imx6q")) {
        if (gpu_clk_mult < 2 || gpu_clk_mult > 64) {
            dev_err(dev, "gpu clock multiplier must be between 2 and 64\n");
            return NOTIFY_OK;
        }
    } else if (of_machine_is_compatible("fsl,imx8qxp") ||
               of_machine_is_compatible("fsl,imx8mm") ||
              (of_machine_is_compatible("fsl,imx8mn"))) {
        if (gpu_clk_mult < 1 || gpu_clk_mult > 64) {
            dev_err(dev, "gpu clock multiplier must be between 1 and 64\n");
            return NOTIFY_OK;
        }
    } else {
        dev_err(dev, "Platform not supported.\n");
        return NOTIFY_OK;
    }

    gckHARDWARE_SetFscaleValue(hardware, (gctUINT)gpu_clk_mult, ~0U);

    dev_dbg(dev, "gpu clock multiplier set to %d\n", (gctUINT)gpu_clk_mult);

    return count;
}
static DEVICE_ATTR(gpu_mult, 0600, gpu_mult_show, gpu_mult_store);

static struct attribute *gpu_sysfs_entries[] = {
    &dev_attr_gpu_mult.attr,
    NULL,
};

static struct attribute_group gpu_attr_group = {
    .name	= NULL,        /* put in device directory */
    .attrs	= gpu_sysfs_entries,
};

static struct file_operations driver_fops =
{
    .owner      = THIS_MODULE,
    .open       = drv_open,
    .release    = drv_release,
    .unlocked_ioctl = drv_ioctl,

#if LINUX_VERSION_CODE <= KERNEL_VERSION(5,8,18)

#ifdef HAVE_COMPAT_IOCTL
    .compat_ioctl = drv_ioctl,
#endif

#else
    .compat_ioctl = drv_ioctl,
#endif
};

static struct miscdevice gal_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = DEVICE_NAME,
    .fops  = &driver_fops,
};

static int drv_init(void)
{
    int result = -EINVAL;
    gceSTATUS status;
    gckGALDEVICE device = gcvNULL;
    struct class* device_class = gcvNULL;

    gcmkHEADER();

    printk(KERN_INFO "Galcore version %s\n", gcvVERSION_STRING);

    if (showArgs)
    {
        gckOS_DumpParam();
    }

    /* Create the GAL device. */
    status = gckGALDEVICE_Construct(platform, &moduleParam, &device);

    if (gcmIS_ERROR(status))
    {
        gcmkTRACE_ZONE(gcvLEVEL_ERROR, gcvZONE_DRIVER,
                       "%s(%d): Failed to create the GAL device: status=%d\n",
                       __FUNCTION__, __LINE__, status);

        goto OnError;
    }

    /* Start the GAL device. */
    gcmkONERROR(gckGALDEVICE_Start(device));

    if ((physSize != 0)
       && (device->kernels[gcvCORE_MAJOR] != gcvNULL)
       && (device->kernels[gcvCORE_MAJOR]->hardware->mmuVersion != 0))
    {
        /* Reset the base address */
        device->baseAddress = 0;
    }

    /* Set global galDevice pointer. */
    galDevice = device;

    if (type == 1)
    {
        /* Register as misc driver. */
        result = misc_register(&gal_device);

        if (result < 0)
        {
            gcmkTRACE_ZONE(
                gcvLEVEL_ERROR, gcvZONE_DRIVER,
                "%s(%d): misc_register fails.\n",
                __FUNCTION__, __LINE__
                );

            gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
        }
    }
    else
    {
        /* Register the character device. */
        result = register_chrdev(major, DEVICE_NAME, &driver_fops);

        if (result < 0)
        {
            gcmkTRACE_ZONE(
                gcvLEVEL_ERROR, gcvZONE_DRIVER,
                "%s(%d): Could not allocate major number for mmap.\n",
                __FUNCTION__, __LINE__
                );

            gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
        }

        if (major == 0)
        {
            major = result;
        }

        /* Create the device class. */
        device_class = class_create(THIS_MODULE, CLASS_NAME);

        if (IS_ERR(device_class))
        {
            gcmkTRACE_ZONE(
                gcvLEVEL_ERROR, gcvZONE_DRIVER,
                "%s(%d): Failed to create the class.\n",
                __FUNCTION__, __LINE__
                );

            gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
        }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
        device_create(device_class, NULL, MKDEV(major, 0), NULL, DEVICE_NAME);
#else
        device_create(device_class, NULL, MKDEV(major, 0), DEVICE_NAME);
#endif

        gpuClass  = device_class;
    }

    gcmkTRACE_ZONE(
        gcvLEVEL_INFO, gcvZONE_DRIVER,
        "%s(%d): irqLine=%d, contiguousSize=%lu, memBase=0x%lX\n",
        __FUNCTION__, __LINE__,
        irqLine, contiguousSize, registerMemBase
        );

    result = sysfs_create_group(&device->platform->device->dev.kobj,
                             &gpu_attr_group);
    if (result) {
        dev_err(&device->platform->device->dev,
                "Cannot create sysfs entries for gpu (%d)\n", result);
        goto OnError;
    }

    /* Success. */
    gcmkFOOTER();
    return 0;

OnError:
    /* Roll back. */
    if (device_class)
    {
        device_destroy(device_class, MKDEV(major, 0));
        class_destroy(device_class);
    }

    if (result < 0)
    {
        if (type == 1)
        {
            misc_deregister(&gal_device);
        }
        else
        {
            unregister_chrdev(result, DEVICE_NAME);
        }
    }
    if (device)
    {
        gcmkVERIFY_OK(gckGALDEVICE_Stop(device));
        gcmkVERIFY_OK(gckGALDEVICE_Destroy(device));
    }

    galcore_device->dma_mask = NULL;
    galcore_device = NULL;

    gcmkFOOTER();
    return result;
}

static void drv_exit(void)
{
    gcmkHEADER();

    if (type == 1)
    {
        misc_deregister(&gal_device);
    }
    else
    {
        gcmkASSERT(gpuClass != gcvNULL);
        device_destroy(gpuClass, MKDEV(major, 0));
        class_destroy(gpuClass);

        unregister_chrdev(major, DEVICE_NAME);
    }

    gcmkVERIFY_OK(gckGALDEVICE_Stop(galDevice));
    gcmkVERIFY_OK(gckGALDEVICE_Destroy(galDevice));

    gcmkFOOTER_NO();
}

#if gcdENABLE_DRM
int viv_drm_probe(struct device *dev);
int viv_drm_remove(struct device *dev);
#endif

struct device *galcore_device = NULL;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
static int gpu_probe(struct platform_device *pdev)
#else
static int __devinit gpu_probe(struct platform_device *pdev)
#endif
{
    int ret = -ENODEV;
    bool getPowerFlag = gcvFALSE;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
    static u64 dma_mask = DMA_BIT_MASK(40);
#else
    static u64 dma_mask = DMA_40BIT_MASK;
#endif

#if gcdCAPTURE_ONLY_MODE
    gctPHYS_ADDR_T contiguousBaseCap = 0;
    gctSIZE_T contiguousSizeCap = 0;
    gctPHYS_ADDR_T sRAMBaseCap[gcvCORE_COUNT][gcvSRAM_INTER_COUNT];
    gctUINT32 sRAMSizeCap[gcvCORE_COUNT][gcvSRAM_INTER_COUNT];
    gctPHYS_ADDR_T extSRAMBaseCap[gcvSRAM_EXT_COUNT];
    gctUINT32 extSRAMSizeCap[gcvSRAM_EXT_COUNT];
    gctUINT i = 0, j = 0;
#endif

    gcmkHEADER();

    platform->device = pdev;
    galcore_device = &pdev->dev;

    galcore_device->dma_mask = &dma_mask;

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)
    galcore_device->coherent_dma_mask = dma_mask;
#endif

    if (platform->ops->getPower)
    {
        if (gcmIS_ERROR(platform->ops->getPower(platform)))
        {
            gcmkFOOTER_NO();
            return ret;
        }
        getPowerFlag = gcvTRUE;
    }

    /* Gather module parameters. */
    _InitModuleParam(&moduleParam);

#if gcdCAPTURE_ONLY_MODE
    contiguousBaseCap = moduleParam.contiguousBase;
    contiguousSizeCap = moduleParam.contiguousSize;

    gcmkPRINT("Capture only mode is enabled in Hal Kernel.");

    if ((contiguousBaseCap + contiguousSizeCap) > 0x80000000)
    {
        gcmkPRINT("Capture only mode: contiguousBase + contiguousSize > 2G, there is error in CModel and old MMU version RTL simulation.");
    }

    for (i = 0; i < gcvCORE_COUNT; i++)
    {
        for (j = 0; j < gcvSRAM_INTER_COUNT; j++)
        {
            sRAMBaseCap[i][j] = moduleParam.sRAMBases[i][j];
            sRAMSizeCap[i][j] = moduleParam.sRAMSizes[i][j];
        }
    }

    for (i = 0; i < gcvSRAM_EXT_COUNT; i++)
    {
        extSRAMBaseCap[i] = moduleParam.extSRAMBases[i];
        extSRAMSizeCap[i] = moduleParam.extSRAMSizes[i];
    }
#endif

    if (platform->ops->adjustParam)
    {
        /* Override default module param. */
        platform->ops->adjustParam(platform, &moduleParam);
    }

#if gcdCAPTURE_ONLY_MODE
    moduleParam.contiguousBase = contiguousBaseCap;
    moduleParam.contiguousSize = contiguousSizeCap;

    for (i = 0; i < gcvCORE_COUNT; i++)
    {
        for (j = 0; j < gcvSRAM_INTER_COUNT; j++)
        {
            moduleParam.sRAMBases[i][j] = sRAMBaseCap[i][j];
            moduleParam.sRAMSizes[i][j] = sRAMSizeCap[i][j];
        }
    }

    for (i = 0; i < gcvSRAM_EXT_COUNT; i++)
    {
        moduleParam.extSRAMBases[i] = extSRAMBaseCap[i];
        moduleParam.extSRAMSizes[i] = extSRAMSizeCap[i];
    }
#endif
    /* Update module param because drv_init() uses them directly. */
    _SyncModuleParam(&moduleParam);

    if (powerManagement == 0)
    {
        gcmkPRINT("[galcore warning]: power saveing is disabled.");
    }

    ret = drv_init();

    if (!ret)
    {
        platform_set_drvdata(pdev, galDevice);

#if gcdENABLE_DRM
        ret = viv_drm_probe(&pdev->dev);
#endif
    }

    if (ret < 0)
    {
        if(platform->ops->putPower)
        {
            if(getPowerFlag == gcvTRUE)
            {
                platform->ops->putPower(platform);
            }
        }

        gcmkPRINT("galcore: retry probe\n");
        ret = -EPROBE_DEFER;
        gcmkFOOTER_ARG(KERN_INFO "probe end ret=%d", ret);
        return ret;
    }
    else
    {
        gcmkFOOTER_NO();
    }

    gcmkFOOTER_ARG(KERN_INFO "Success ret=%d", ret);
    return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
static int gpu_remove(struct platform_device *pdev)
#else
 static int __devexit gpu_remove(struct platform_device *pdev)
#endif
{
    gcmkHEADER();

#if gcdENABLE_DRM
    viv_drm_remove(&pdev->dev);
#endif

    drv_exit();

    if (platform->ops->putPower)
    {
        platform->ops->putPower(platform);
    }

    galcore_device->dma_mask = NULL;
    galcore_device = NULL;
    gcmkFOOTER_NO();
    return 0;
}

static int gpu_suspend(struct platform_device *dev, pm_message_t state)
{
    gceSTATUS status;
    gckGALDEVICE device = platform_get_drvdata(dev);

    if (!device)
    {
        return -1;
    }

    /* Power off the GPU. */
    status = gckGALDEVICE_Suspend(device, gcvPOWER_OFF);

    if (gcmIS_ERROR(status))
        return -1;

    return 0;
}

static int gpu_resume(struct platform_device *dev)
{
    gceSTATUS status;

    gckGALDEVICE device = platform_get_drvdata(dev);

    if (!device)
    {
        return -1;
    }

    /* Resume GPU to previous state. */
    status = gckGALDEVICE_Resume(device);

    if (gcmIS_ERROR(status))
        return -1;

    return 0;
}

#if defined(CONFIG_PM) && LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 30)
#ifdef CONFIG_PM_SLEEP
static int gpu_system_suspend(struct device *dev)
{
    pm_message_t state={0};
    return gpu_suspend(to_platform_device(dev), state);
}

static int gpu_system_resume(struct device *dev)
{
    return gpu_resume(to_platform_device(dev));
}
#endif

static const struct dev_pm_ops gpu_pm_ops = {
    SET_SYSTEM_SLEEP_PM_OPS(gpu_system_suspend, gpu_system_resume)
};
#endif

static struct platform_driver gpu_driver = {
    .probe      = gpu_probe,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
    .remove     = gpu_remove,
#else
    .remove     = __devexit_p(gpu_remove),
#endif

    .suspend    = gpu_suspend,
    .resume     = gpu_resume,

    .driver     = {
        .owner = THIS_MODULE,
        .name   = DEVICE_NAME,
#if defined(CONFIG_PM) && LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 30)
        .pm     = &gpu_pm_ops,
#endif
    }
};

static int __init gpu_init(void)
{
    int ret = 0;

    ret = gckPLATFORM_Init(&gpu_driver, &platform);

    if (ret || !platform)
    {
        printk(KERN_ERR "galcore: Soc platform init failed.\n");
        return -ENODEV;
    }

    ret = platform_driver_register(&gpu_driver);

    if (ret)
    {
        printk(KERN_ERR "galcore: gpu_init() failed to register driver!\n");
        gckPLATFORM_Terminate(platform);
        platform = NULL;
        return -ENODEV;
    }

    platform->driver = &gpu_driver;
    return 0;
}

static void __exit gpu_exit(void)
{
    platform_driver_unregister(&gpu_driver);

    gckPLATFORM_Terminate(platform);
    platform = NULL;
}

module_init(gpu_init);

module_exit(gpu_exit);
