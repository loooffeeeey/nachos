// addrspace.cc 
//	Routines to manage address spaces (executing user programs).
//
//	In order to run a user program, you must:
//
//	1. link with the -N -T 0 option 
//	2. run coff2noff to convert the object file to Nachos format
//		(Nachos object code format is essentially just a simpler
//		version of the UNIX executable object code format)
//	3. load the NOFF file into the Nachos file system
//		(if you haven't implemented the file system yet, you
//		don't need to do this last step)
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "addrspace.h"
#include "noff.h"
#ifdef HOST_SPARC
#include <strings.h>
#endif

//----------------------------------------------------------------------
// SwapHeader
// 	Do little endian to big endian conversion on the bytes in the 
//	object file header, in case the file was generated on a little
//	endian machine, and we're now running on a big endian machine.
//----------------------------------------------------------------------

static void 
SwapHeader (NoffHeader *noffH)
{
	noffH->noffMagic = WordToHost(noffH->noffMagic);
	noffH->code.size = WordToHost(noffH->code.size);
	noffH->code.virtualAddr = WordToHost(noffH->code.virtualAddr);
	noffH->code.inFileAddr = WordToHost(noffH->code.inFileAddr);
	noffH->initData.size = WordToHost(noffH->initData.size);
	noffH->initData.virtualAddr = WordToHost(noffH->initData.virtualAddr);
	noffH->initData.inFileAddr = WordToHost(noffH->initData.inFileAddr);
	noffH->uninitData.size = WordToHost(noffH->uninitData.size);
	noffH->uninitData.virtualAddr = WordToHost(noffH->uninitData.virtualAddr);
	noffH->uninitData.inFileAddr = WordToHost(noffH->uninitData.inFileAddr);
}

//----------------------------------------------------------------------
// AddrSpace::AddrSpace
// 	Create an address space to run a user program.
//	Load the program from a file "executable", and set everything
//	up so that we can start executing user instructions.
//
//	Assumes that the object code file is in NOFF format.
//
//	First, set up the translation from program memory to physical 
//	memory.  For now, this is really simple (1:1), since we are
//	only uniprogramming, and we have a single unsegmented page table
//
//	"executable" is the file containing the object code to load into memory
//----------------------------------------------------------------------

AddrSpace::AddrSpace(OpenFile *executable)
{
    NoffHeader noffH;
    unsigned int i, size;

    executable->ReadAt((char *)&noffH, sizeof(noffH), 0);
    if ((noffH.noffMagic != NOFFMAGIC) && (WordToHost(noffH.noffMagic) == NOFFMAGIC))
    	SwapHeader(&noffH);
    ASSERT(noffH.noffMagic == NOFFMAGIC);

// how big is address space?
    size = noffH.code.size + noffH.initData.size + noffH.uninitData.size 
			+ UserStackSize;              ////////////程序在内存中占多少字节

    numPages = divRoundUp(size, PageSize);////////////程序会占几页

    size = numPages * PageSize;///////////////////////程序需要多少字节的内存

    ASSERT(numPages <= NumPhysPages);/////////////////如果运行的程序大小比内存还大那就报

    DEBUG('a', "Initializing address space, num pages %d, size %d\n", numPages, size);///////////程序有多大，占几页

	
    pageTable = new TranslationEntry[numPages];///////////////////程序的页表，大小等于程序的内存页数
    
    for (i = 0; i < numPages; i++) {
		pageTable[i].virtualPage = i;//////////////////////////////程序的逻辑地址页
		pageTable[i].physicalPage = i;/////////////////////////////程序的物理地页号，为什么页等于i和逻辑地址页相同？？？？？
		pageTable[i].valid = TRUE;/////////////////////////////////程序要分配地址了，肯定是有效地
		pageTable[i].use = FALSE;//////////////////////////////////目前这一页才放入内存，并没有被用过
		pageTable[i].dirty = FALSE;////////////////////////////////更没有被修改过
		pageTable[i].readOnly = FALSE;  // ////////////////////////如果有要求的话，也就是代码段和数据段分开，代码段要求只读
					// a separate page, we could set its 
					// pages to be read-only
    }
    
// zero out the entire address space, to zero the unitialized data segment 
// and the stack segment
    bzero(machine->mainMemory, size);///////////////////////////////目前是没有多线程支持的，也就是说，有多大的
    												////////////////size，就在内存中从最开始清空多少，腾出空间来

// then, copy in the code and data segments into memory


    if (noffH.code.size > 0) {//////////////////////////////////////////在文件中相应位置开始，把代码段放入内存中相应位置,虚拟内存未实现
    																								       //////////实现了则是虚拟内存
        DEBUG('a', "Initializing code segment, at 0x%x, size %d\n", noffH.code.virtualAddr, noffH.code.size);
        executable->ReadAt(&(machine->mainMemory[noffH.code.virtualAddr]),noffH.code.size, noffH.code.inFileAddr);
    }
    if (noffH.initData.size > 0) {///////////////////////////////////////////////////////////////这里是同样结构，即数据段了，需要初始化的
        DEBUG('a', "Initializing data segment, at 0x%x, size %d\n", noffH.initData.virtualAddr, noffH.initData.size);
        executable->ReadAt(&(machine->mainMemory[noffH.initData.virtualAddr]),noffH.initData.size, noffH.initData.inFileAddr);
    }

}

//----------------------------------------------------------------------
// AddrSpace::~AddrSpace
// 	Dealloate an address space.  Nothing for now!
//----------------------------------------------------------------------

AddrSpace::~AddrSpace()//////////////////////////////////////////////////////////////////直接删掉了代表着逻辑地址的表
{
   delete pageTable;
}

//----------------------------------------------------------------------
// AddrSpace::InitRegisters
// 	Set the initial values for the user-level register set.
//
// 	We write these directly into the "machine" registers, so
//	that we can immediately jump to user code.  Note that these
//	will be saved/restored into the currentThread->userRegisters
//	when this thread is context switched out.
//----------------------------------------------------------------------

void
AddrSpace::InitRegisters()      //////线程寄存器结构体中的内容是存放在我的电脑内存里并不是MIPS里，这里很假                                                   
{
    int i;

    for (i = 0; i < NumTotalRegs; i++)//////////////////////////////////初始化40个寄存器
 		machine->WriteRegister(i, 0);

    machine->WriteRegister(PCReg, 0);///////////////////////////////////PC寄存器	

    // Need to also tell MIPS where next instruction is, because
    // of branch delay possibility
    machine->WriteRegister(NextPCReg, 4);///////////////////////////////////这里的“延迟”有点不懂是为了什么？

   // Set the stack register to the end of the address space, where we
   // allocated the stack; but subtract off a bit, to make sure we don't
   // accidentally reference off the end!
    machine->WriteRegister(StackReg, numPages * PageSize - 16);/////////////////////栈寄存器中放指向栈的指针，减去16防止越界
    DEBUG('a', "Initializing stack register to %d\n", numPages * PageSize - 16);
}

//----------------------------------------------------------------------
// AddrSpace::SaveState
// 	On a context switch, save any machine state, specific
//	to this address space, that needs saving.
//
//	For now, nothing!
//----------------------------------------------------------------------

void AddrSpace::SaveState() 
{
	
}

//----------------------------------------------------------------------
// AddrSpace::RestoreState
// 	On a context switch, restore the machine state so that
//	this address space can run.
//
//      For now, tell the machine where to find the page table.
//----------------------------------------------------------------------

void AddrSpace::RestoreState() 
{
    machine->pageTable = pageTable;
    machine->pageTableSize = numPages;
}
