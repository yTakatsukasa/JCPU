#include "jcpu_llvm_headers.h"

namespace jcpu{
namespace vm{

namespace{
inline llvm::GetElementPtrInst *
CreateGetElementPtrInst(llvm::Value *Ptr, llvm::ArrayRef<llvm::Value *> IdxList,
        const char *NameStr, llvm::BasicBlock *InsertAtEnd, llvm::Module &mod)
{
#if JCPU_LLVM_VERSION_LT(3, 7)
    (void) mod;
    return llvm::GetElementPtrInst::Create(Ptr, IdxList, NameStr, InsertAtEnd);
#else
    llvm::Type *const pointee_type = llvm::Type::getPrimitiveType(mod.getContext(), llvm::Type::TypeID::ArrayTyID);
    return llvm::GetElementPtrInst::Create(pointee_type, Ptr, IdxList, NameStr, InsertAtEnd);
#endif
}

}


//FIXME set register bit width
void make_set_get(llvm::Module *mod, llvm::GlobalVariable *gvar_array_regs, unsigned int address_space) {
    using namespace llvm;

    // Type Definitions
    std::vector<Type*>FuncTy_2_args;
    FuncTy_2_args.push_back(IntegerType::get(mod->getContext(), 16));
    FunctionType* FuncTy_2 = FunctionType::get(
            /*Result=*/IntegerType::get(mod->getContext(), 32),
            /*Params=*/FuncTy_2_args,
            /*isVarArg=*/false);


    std::vector<Type*>FuncTy_4_args;
    FuncTy_4_args.push_back(IntegerType::get(mod->getContext(), 16));
    FuncTy_4_args.push_back(IntegerType::get(mod->getContext(), 32));
    FunctionType* FuncTy_4 = FunctionType::get(
            /*Result=*/Type::getVoidTy(mod->getContext()),
            /*Params=*/FuncTy_4_args,
            /*isVarArg=*/false);


    // Function Declarations

    Function* func_get_reg = mod->getFunction("get_reg");
    if (!func_get_reg) {
        func_get_reg = Function::Create(
                /*Type=*/FuncTy_2,
                /*Linkage=*/GlobalValue::ExternalLinkage,
                /*Name=*/"get_reg", mod); 
        func_get_reg->setCallingConv(CallingConv::C);
    }
    Function* func_set_reg = mod->getFunction("set_reg");
    if (!func_set_reg) {
        func_set_reg = Function::Create(
                /*Type=*/FuncTy_4,
                /*Linkage=*/GlobalValue::ExternalLinkage,
                /*Name=*/"set_reg", mod); 
        func_set_reg->setCallingConv(CallingConv::C);
    }
    // Global Variable Declarations

    ConstantInt* const_int64_6 = ConstantInt::get(mod->getContext(), APInt(64, 0));

    // Function Definitions

    // Function: get_reg (func_get_reg)
    {
        Function::arg_iterator args = func_get_reg->arg_begin();
        Value* int16_idx = args++;
        int16_idx->setName("idx");

        BasicBlock* label_7 = BasicBlock::Create(mod->getContext(), "",func_get_reg,0);

        // Block  (label_7)
        CastInst* int64_8 = new ZExtInst(int16_idx, IntegerType::get(mod->getContext(), 64), "", label_7);
        std::vector<Value*> ptr_9_indices;
        ptr_9_indices.push_back(const_int64_6);
        ptr_9_indices.push_back(int64_8);
        Instruction *ptr_9 = CreateGetElementPtrInst(gvar_array_regs, ptr_9_indices, "", label_7, *mod);
        LoadInst* int32_10 = new LoadInst(ptr_9, "", false, label_7);
        int32_10->setAlignment(4);
        ReturnInst::Create(mod->getContext(), int32_10, label_7);

    }

    // Function: set_reg (func_set_reg)
    {
        Function::arg_iterator args = func_set_reg->arg_begin();
        Value* int16_idx_12 = args++;
        int16_idx_12->setName("idx");
        Value* int32_val = args++;
        int32_val->setName("val");

        BasicBlock* label_13 = BasicBlock::Create(mod->getContext(), "",func_set_reg,0);

        // Block  (label_13)
        CastInst* int64_14 = new ZExtInst(int16_idx_12, IntegerType::get(mod->getContext(), 64), "", label_13);
        std::vector<Value*> ptr_15_indices;
        ptr_15_indices.push_back(const_int64_6);
        ptr_15_indices.push_back(int64_14);
        Instruction* ptr_15 = CreateGetElementPtrInst(gvar_array_regs, ptr_15_indices, "", label_13, *mod);
        StoreInst* void_16 = new StoreInst(int32_val, ptr_15, false, label_13);
        void_16->setAlignment(4);
        ReturnInst::Create(mod->getContext(), label_13);

    }

}


void make_mem_access(llvm::Module *mod, unsigned int address_space) {
    using namespace llvm;
    // Module Construction
    // Type Definitions
    StructType *StructTy_class_jcpu__jcpu_ext_if = mod->getTypeByName("class.jcpu::jcpu_ext_if");
    if (!StructTy_class_jcpu__jcpu_ext_if) {
        StructTy_class_jcpu__jcpu_ext_if = StructType::create(mod->getContext(), "class.jcpu::jcpu_ext_if");
    }
    std::vector<Type*>StructTy_class_jcpu__jcpu_ext_if_fields;
    std::vector<Type*>FuncTy_3_args;
    FunctionType* FuncTy_3 = FunctionType::get(
            /*Result=*/IntegerType::get(mod->getContext(), 32),
            /*Params=*/FuncTy_3_args,
            /*isVarArg=*/true);

    PointerType* PointerTy_2 = PointerType::get(FuncTy_3, address_space);

    PointerType* PointerTy_1 = PointerType::get(PointerTy_2, address_space);

    StructTy_class_jcpu__jcpu_ext_if_fields.push_back(PointerTy_1);
    if (StructTy_class_jcpu__jcpu_ext_if->isOpaque()) {
        StructTy_class_jcpu__jcpu_ext_if->setBody(StructTy_class_jcpu__jcpu_ext_if_fields, /*isPacked=*/false);
    }

    PointerType* PointerTy_0 = PointerType::get(StructTy_class_jcpu__jcpu_ext_if, address_space);

    std::vector<Type*>FuncTy_5_args;
    FuncTy_5_args.push_back(PointerTy_0);
    FunctionType* FuncTy_5 = FunctionType::get(
            /*Result=*/Type::getVoidTy(mod->getContext()),
            /*Params=*/FuncTy_5_args,
            /*isVarArg=*/false);

    std::vector<Type*>FuncTy_6_args;
    FuncTy_6_args.push_back(IntegerType::get(mod->getContext(), 64));
    FuncTy_6_args.push_back(IntegerType::get(mod->getContext(), 32));
    FunctionType* FuncTy_6 = FunctionType::get(
            /*Result=*/IntegerType::get(mod->getContext(), 64),
            /*Params=*/FuncTy_6_args,
            /*isVarArg=*/false);

    std::vector<Type*>FuncTy_10_args;
    FuncTy_10_args.push_back(PointerTy_0);
    FuncTy_10_args.push_back(IntegerType::get(mod->getContext(), 64));
    FuncTy_10_args.push_back(IntegerType::get(mod->getContext(), 32));
    FunctionType* FuncTy_10 = FunctionType::get(
            /*Result=*/IntegerType::get(mod->getContext(), 64),
            /*Params=*/FuncTy_10_args,
            /*isVarArg=*/false);

    PointerType* PointerTy_9 = PointerType::get(FuncTy_10, address_space);

    PointerType* PointerTy_8 = PointerType::get(PointerTy_9, address_space);

    PointerType* PointerTy_7 = PointerType::get(PointerTy_8, address_space);

    std::vector<Type*>FuncTy_11_args;
    FuncTy_11_args.push_back(IntegerType::get(mod->getContext(), 64));
    FuncTy_11_args.push_back(IntegerType::get(mod->getContext(), 32));
    FuncTy_11_args.push_back(IntegerType::get(mod->getContext(), 64));
    FunctionType* FuncTy_11 = FunctionType::get(
            /*Result=*/Type::getVoidTy(mod->getContext()),
            /*Params=*/FuncTy_11_args,
            /*isVarArg=*/false);

    std::vector<Type*>FuncTy_15_args;
    FuncTy_15_args.push_back(PointerTy_0);
    FuncTy_15_args.push_back(IntegerType::get(mod->getContext(), 64));
    FuncTy_15_args.push_back(IntegerType::get(mod->getContext(), 32));
    FuncTy_15_args.push_back(IntegerType::get(mod->getContext(), 64));
    FunctionType* FuncTy_15 = FunctionType::get(
            /*Result=*/Type::getVoidTy(mod->getContext()),
            /*Params=*/FuncTy_15_args,
            /*isVarArg=*/false);

    PointerType* PointerTy_14 = PointerType::get(FuncTy_15, address_space);

    PointerType* PointerTy_13 = PointerType::get(PointerTy_14, address_space);

    PointerType* PointerTy_12 = PointerType::get(PointerTy_13, address_space);


    // Function Declarations

    Function* func_set_mem_access_if = mod->getFunction("set_mem_access_if");
    if (!func_set_mem_access_if) {
        func_set_mem_access_if = Function::Create(
                /*Type=*/FuncTy_5,
                /*Linkage=*/GlobalValue::ExternalLinkage,
                /*Name=*/"set_mem_access_if", mod); 
        func_set_mem_access_if->setCallingConv(CallingConv::C);
    }
    Function* func_helper_mem_read = mod->getFunction("helper_mem_read");
    if (!func_helper_mem_read) {
        func_helper_mem_read = Function::Create(
                /*Type=*/FuncTy_6,
                /*Linkage=*/GlobalValue::ExternalLinkage,
                /*Name=*/"helper_mem_read", mod); 
        func_helper_mem_read->setCallingConv(CallingConv::C);
    }
    Function* func_helper_mem_write = mod->getFunction("helper_mem_write");
    if (!func_helper_mem_write) {
        func_helper_mem_write = Function::Create(
                /*Type=*/FuncTy_11,
                /*Linkage=*/GlobalValue::ExternalLinkage,
                /*Name=*/"helper_mem_write", mod); 
        func_helper_mem_write->setCallingConv(CallingConv::C);
    }
    Function* func_helper_mem_read_debug = mod->getFunction("helper_mem_read_debug");
    if (!func_helper_mem_read_debug) {
        func_helper_mem_read_debug = Function::Create(
                /*Type=*/FuncTy_6,
                /*Linkage=*/GlobalValue::ExternalLinkage,
                /*Name=*/"helper_mem_read_debug", mod); 
        func_helper_mem_read_debug->setCallingConv(CallingConv::C);
    }
    Function* func_helper_mem_write_debug = mod->getFunction("helper_mem_write_debug");
    if (!func_helper_mem_write_debug) {
        func_helper_mem_write_debug = Function::Create(
                /*Type=*/FuncTy_11,
                /*Linkage=*/GlobalValue::ExternalLinkage,
                /*Name=*/"helper_mem_write_debug", mod); 
        func_helper_mem_write_debug->setCallingConv(CallingConv::C);
    }
    // Global Variable Declarations


    GlobalVariable* gvar_ptr_mem_access_if = new GlobalVariable(/*Module=*/*mod, 
            /*Type=*/PointerTy_0,
            /*isConstant=*/false,
            /*Linkage=*/GlobalValue::ExternalLinkage,
            /*Initializer=*/0, // has initializer, specified below
            /*Name=*/"mem_access_if");
    gvar_ptr_mem_access_if->setAlignment(8);

    // Constant Definitions
    ConstantPointerNull* const_ptr_16 = ConstantPointerNull::get(PointerTy_0);
    ConstantInt* const_int64_17 = ConstantInt::get(mod->getContext(), APInt(64, StringRef("1"), 10));
    ConstantInt* const_int64_18 = ConstantInt::get(mod->getContext(), APInt(64, StringRef("2"), 10));
    ConstantInt* const_int64_19 = ConstantInt::get(mod->getContext(), APInt(64, StringRef("3"), 10));

    // Global Variable Definitions
    gvar_ptr_mem_access_if->setInitializer(const_ptr_16);

    // Function Definitions

    // Function: set_mem_access_if (func_set_mem_access_if)
    {
        Function::arg_iterator args = func_set_mem_access_if->arg_begin();
        Value* ptr_ifs = args++;
        ptr_ifs->setName("ifs");

        BasicBlock* label_20 = BasicBlock::Create(mod->getContext(), "",func_set_mem_access_if,0);

        // Block  (label_20)
        StoreInst* void_21 = new StoreInst(ptr_ifs, gvar_ptr_mem_access_if, false, label_20);
        void_21->setAlignment(8);
        ReturnInst::Create(mod->getContext(), label_20);

    }

    // Function: helper_mem_read (func_helper_mem_read)
    {
        Function::arg_iterator args = func_helper_mem_read->arg_begin();
        Value* int64_addr = args++;
        int64_addr->setName("addr");
        Value* int32_length = args++;
        int32_length->setName("length");

        BasicBlock* label_23 = BasicBlock::Create(mod->getContext(), "",func_helper_mem_read,0);

        // Block  (label_23)
        LoadInst* ptr_24 = new LoadInst(gvar_ptr_mem_access_if, "", false, label_23);
        ptr_24->setAlignment(8);
        CastInst* ptr_25 = new BitCastInst(ptr_24, PointerTy_7, "", label_23);
        LoadInst* ptr_26 = new LoadInst(ptr_25, "", false, label_23);
        ptr_26->setAlignment(8);
        LoadInst* ptr_27 = new LoadInst(ptr_26, "", false, label_23);
        ptr_27->setAlignment(8);
        std::vector<Value*> int64_28_params;
        int64_28_params.push_back(ptr_24);
        int64_28_params.push_back(int64_addr);
        int64_28_params.push_back(int32_length);
        CallInst* int64_28 = CallInst::Create(ptr_27, int64_28_params, "", label_23);
        int64_28->setCallingConv(CallingConv::C);
        int64_28->setTailCall(true);

        ReturnInst::Create(mod->getContext(), int64_28, label_23);

    }

    // Function: helper_mem_write (func_helper_mem_write)
    {
        Function::arg_iterator args = func_helper_mem_write->arg_begin();
        Value* int64_addr_30 = args++;
        int64_addr_30->setName("addr");
        Value* int32_length_31 = args++;
        int32_length_31->setName("length");
        Value* int64_val = args++;
        int64_val->setName("val");

        BasicBlock* label_32 = BasicBlock::Create(mod->getContext(), "",func_helper_mem_write,0);

        // Block  (label_32)
        LoadInst* ptr_33 = new LoadInst(gvar_ptr_mem_access_if, "", false, label_32);
        ptr_33->setAlignment(8);
        CastInst* ptr_34 = new BitCastInst(ptr_33, PointerTy_12, "", label_32);
        LoadInst* ptr_35 = new LoadInst(ptr_34, "", false, label_32);
        ptr_35->setAlignment(8);
        GetElementPtrInst* ptr_36 = CreateGetElementPtrInst(ptr_35, const_int64_17, "", label_32, *mod);
        LoadInst* ptr_37 = new LoadInst(ptr_36, "", false, label_32);
        ptr_37->setAlignment(8);
        std::vector<Value*> void_38_params;
        void_38_params.push_back(ptr_33);
        void_38_params.push_back(int64_addr_30);
        void_38_params.push_back(int32_length_31);
        void_38_params.push_back(int64_val);
        CallInst* void_38 = CallInst::Create(ptr_37, void_38_params, "", label_32);
        void_38->setCallingConv(CallingConv::C);
        void_38->setTailCall(true);

        ReturnInst::Create(mod->getContext(), label_32);

    }

    // Function: helper_mem_read_debug (func_helper_mem_read_debug)
    {
        Function::arg_iterator args = func_helper_mem_read_debug->arg_begin();
        Value* int64_addr_40 = args++;
        int64_addr_40->setName("addr");
        Value* int32_length_41 = args++;
        int32_length_41->setName("length");

        BasicBlock* label_42 = BasicBlock::Create(mod->getContext(), "",func_helper_mem_read_debug,0);

        // Block  (label_42)
        LoadInst* ptr_43 = new LoadInst(gvar_ptr_mem_access_if, "", false, label_42);
        ptr_43->setAlignment(8);
        CastInst* ptr_44 = new BitCastInst(ptr_43, PointerTy_7, "", label_42);
        LoadInst* ptr_45 = new LoadInst(ptr_44, "", false, label_42);
        ptr_45->setAlignment(8);
        GetElementPtrInst* ptr_46 = CreateGetElementPtrInst(ptr_45, const_int64_18, "", label_42, *mod);
        LoadInst* ptr_47 = new LoadInst(ptr_46, "", false, label_42);
        ptr_47->setAlignment(8);
        std::vector<Value*> int64_48_params;
        int64_48_params.push_back(ptr_43);
        int64_48_params.push_back(int64_addr_40);
        int64_48_params.push_back(int32_length_41);
        CallInst* int64_48 = CallInst::Create(ptr_47, int64_48_params, "", label_42);
        int64_48->setCallingConv(CallingConv::C);
        int64_48->setTailCall(true);

        ReturnInst::Create(mod->getContext(), int64_48, label_42);

    }

    // Function: helper_mem_write_debug (func_helper_mem_write_debug)
    {
        Function::arg_iterator args = func_helper_mem_write_debug->arg_begin();
        Value* int64_addr_50 = args++;
        int64_addr_50->setName("addr");
        Value* int32_length_51 = args++;
        int32_length_51->setName("length");
        Value* int64_val_52 = args++;
        int64_val_52->setName("val");

        BasicBlock* label_53 = BasicBlock::Create(mod->getContext(), "",func_helper_mem_write_debug,0);

        // Block  (label_53)
        LoadInst* ptr_54 = new LoadInst(gvar_ptr_mem_access_if, "", false, label_53);
        ptr_54->setAlignment(8);
        CastInst* ptr_55 = new BitCastInst(ptr_54, PointerTy_12, "", label_53);
        LoadInst* ptr_56 = new LoadInst(ptr_55, "", false, label_53);
        ptr_56->setAlignment(8);
        GetElementPtrInst* ptr_57 = CreateGetElementPtrInst(ptr_56, const_int64_19, "", label_53, *mod);
        LoadInst* ptr_58 = new LoadInst(ptr_57, "", false, label_53);
        ptr_58->setAlignment(8);
        std::vector<Value*> void_59_params;
        void_59_params.push_back(ptr_54);
        void_59_params.push_back(int64_addr_50);
        void_59_params.push_back(int32_length_51);
        void_59_params.push_back(int64_val_52);
        CallInst* void_59 = CallInst::Create(ptr_58, void_59_params, "", label_53);
        void_59->setCallingConv(CallingConv::C);
        void_59->setTailCall(true);

        ReturnInst::Create(mod->getContext(), label_53);

    }
}


void make_debug_func(llvm::Module *mod, unsigned int address_space) {
    using namespace llvm;

    // Type Definitions
    StructType *StructTy_class_jcpu__vm__jcpu_vm_if = mod->getTypeByName("class.jcpu::vm::jcpu_vm_if");
    if (!StructTy_class_jcpu__vm__jcpu_vm_if) {
        StructTy_class_jcpu__vm__jcpu_vm_if = StructType::create(mod->getContext(), "class.jcpu::vm::jcpu_vm_if");
    }
    std::vector<Type*>StructTy_class_jcpu__vm__jcpu_vm_if_fields;
    std::vector<Type*>FuncTy_3_args;
    FunctionType* FuncTy_3 = FunctionType::get(
            /*Result=*/IntegerType::get(mod->getContext(), 32),
            /*Params=*/FuncTy_3_args,
            /*isVarArg=*/true);

    PointerType* PointerTy_2 = PointerType::get(FuncTy_3, address_space);

    PointerType* PointerTy_1 = PointerType::get(PointerTy_2, address_space);

    StructTy_class_jcpu__vm__jcpu_vm_if_fields.push_back(PointerTy_1);
    if (StructTy_class_jcpu__vm__jcpu_vm_if->isOpaque()) {
        StructTy_class_jcpu__vm__jcpu_vm_if->setBody(StructTy_class_jcpu__vm__jcpu_vm_if_fields, /*isPacked=*/false);
    }

    PointerType* PointerTy_0 = PointerType::get(StructTy_class_jcpu__vm__jcpu_vm_if, address_space);

    std::vector<Type*>FuncTy_5_args;
    FuncTy_5_args.push_back(PointerTy_0);
    FunctionType* FuncTy_5 = FunctionType::get(
            /*Result=*/Type::getVoidTy(mod->getContext()),
            /*Params=*/FuncTy_5_args,
            /*isVarArg=*/false);

    std::vector<Type*>FuncTy_6_args;
    FunctionType* FuncTy_6 = FunctionType::get(
            /*Result=*/Type::getVoidTy(mod->getContext()),
            /*Params=*/FuncTy_6_args,
            /*isVarArg=*/false);

    PointerType* PointerTy_9 = PointerType::get(FuncTy_5, address_space);

    PointerType* PointerTy_8 = PointerType::get(PointerTy_9, address_space);

    PointerType* PointerTy_7 = PointerType::get(PointerTy_8, address_space);


    // Function Declarations

    Function* func_set_jcpu_vm_ptr = mod->getFunction("set_jcpu_vm_ptr");
    if (!func_set_jcpu_vm_ptr) {
        func_set_jcpu_vm_ptr = Function::Create(
                /*Type=*/FuncTy_5,
                /*Linkage=*/GlobalValue::ExternalLinkage,
                /*Name=*/"set_jcpu_vm_ptr", mod); 
        func_set_jcpu_vm_ptr->setCallingConv(CallingConv::C);
    }
    Function* func_jcpu_vm_dump_regs = mod->getFunction("jcpu_vm_dump_regs");
    if (!func_jcpu_vm_dump_regs) {
        func_jcpu_vm_dump_regs = Function::Create(
                /*Type=*/FuncTy_6,
                /*Linkage=*/GlobalValue::ExternalLinkage,
                /*Name=*/"jcpu_vm_dump_regs", mod); 
        func_jcpu_vm_dump_regs->setCallingConv(CallingConv::C);
    }
    // Global Variable Declarations


    GlobalVariable* gvar_ptr_jcpu_vm_ptr = new GlobalVariable(/*Module=*/*mod, 
            /*Type=*/PointerTy_0,
            /*isConstant=*/false,
            /*Linkage=*/GlobalValue::ExternalLinkage,
            /*Initializer=*/0, // has initializer, specified below
            /*Name=*/"jcpu_vm_ptr");
    gvar_ptr_jcpu_vm_ptr->setAlignment(8);

    // Constant Definitions
    ConstantPointerNull* const_ptr_10 = ConstantPointerNull::get(PointerTy_0);

    // Global Variable Definitions
    gvar_ptr_jcpu_vm_ptr->setInitializer(const_ptr_10);

    // Function Definitions

    // Function: set_jcpu_vm_ptr (func_set_jcpu_vm_ptr)
    {
        Function::arg_iterator args = func_set_jcpu_vm_ptr->arg_begin();
        Value* ptr_ptr = args++;
        ptr_ptr->setName("ptr");

        BasicBlock* label_11 = BasicBlock::Create(mod->getContext(), "",func_set_jcpu_vm_ptr,0);

        // Block  (label_11)
        StoreInst* void_12 = new StoreInst(ptr_ptr, gvar_ptr_jcpu_vm_ptr, false, label_11);
        void_12->setAlignment(8);
        ReturnInst::Create(mod->getContext(), label_11);

    }

    // Function: jcpu_vm_dump_regs (func_jcpu_vm_dump_regs)
    {

        BasicBlock* label_14 = BasicBlock::Create(mod->getContext(), "",func_jcpu_vm_dump_regs,0);

        // Block  (label_14)
        LoadInst* ptr_15 = new LoadInst(gvar_ptr_jcpu_vm_ptr, "", false, label_14);
        ptr_15->setAlignment(8);
        CastInst* ptr_16 = new BitCastInst(ptr_15, PointerTy_7, "", label_14);
        LoadInst* ptr_17 = new LoadInst(ptr_16, "", false, label_14);
        ptr_17->setAlignment(8);
        LoadInst* ptr_18 = new LoadInst(ptr_17, "", false, label_14);
        ptr_18->setAlignment(8);
        CallInst* void_19 = CallInst::Create(ptr_18, ptr_15, "", label_14);
        void_19->setCallingConv(CallingConv::C);
        void_19->setTailCall(true);

        ReturnInst::Create(mod->getContext(), label_14);

    }



}
} //end of namespace vm
} //end of namespace jcpu
