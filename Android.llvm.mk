ifeq ($(MESA_LLVM),true)

LOCAL_STATIC_LIBRARIES += \
	libLLVMMCJIT \
	libLLVMRuntimeDyld \
	libLLVMObject \
	libLLVMMCDisassembler \
	libLLVMLinker \
	libLLVMipo \
	libLLVMInterpreter \
	libLLVMInstrumentation \
	libLLVMJIT \
	libLLVMExecutionEngine \
	libLLVMBitWriter

ifeq ($(TARGET_ARCH),x86)
LOCAL_STATIC_LIBRARIES += \
	libLLVMX86Disassembler \
	libLLVMX86AsmParser \
	libLLVMX86CodeGen \
	libLLVMX86Desc \
	libLLVMSelectionDAG \
	libLLVMX86AsmPrinter \
	libLLVMX86Utils \
	libLLVMX86Info
endif

ifeq ($(TARGET_ARCH),arm)
LOCAL_STATIC_LIBRARIES += \
	libLLVMARMDisassembler \
	libLLVMARMCodeGen \
	libLLVMARMDesc \
	libLLVMSelectionDAG \
	libLLVMARMAsmPrinter \
	libLLVMARMInfo
endif

LOCAL_STATIC_LIBRARIES += \
	libLLVMAsmPrinter \
	libLLVMMCParser \
	libLLVMCodeGen \
	libLLVMScalarOpts \
	libLLVMInstCombine \
	libLLVMTransformUtils \
	libLLVMipa \
	libLLVMAsmParser \
	libLLVMArchive \
	libLLVMBitReader \
	libLLVMAnalysis \
	libLLVMTarget \
	libLLVMMC \
	libLLVMCore \
	libLLVMSupport

LOCAL_C_INCLUDES += \
	$(LLVM_ROOT_PATH)/include \
	$(LLVM_ROOT_PATH)/include

LOCAL_CFLAGS += \
	-DHAVE_LLVM=0x0302

include $(LLVM_ROOT_PATH)/llvm-device-build.mk

LOCAL_SHARED_LIBRARIES += libstlport
include external/stlport/libstlport.mk

endif
