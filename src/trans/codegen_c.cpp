/*
 * MRustC - Rust Compiler
 * - By John Hodge (Mutabah/thePowersGang)
 *
 * trans/codegen_c.cpp
 * - Code generation emitting C code
 */
#include "codegen.hpp"
#include "mangling.hpp"
#include <fstream>
#include <hir/hir.hpp>
#include <mir/mir.hpp>
#include <hir_typeck/static.hpp>
#include <mir/helpers.hpp>


namespace {
    class CodeGenerator_C:
        public CodeGenerator
    {
        const ::HIR::Crate& m_crate;
        ::StaticTraitResolve    m_resolve;
        ::std::ofstream m_of;
        const ::MIR::TypeResolve* m_mir_res;
    public:
        CodeGenerator_C(const ::HIR::Crate& crate, const ::std::string& outfile):
            m_crate(crate),
            m_resolve(crate),
            m_of(outfile)
        {
            m_of
                << "/*\n"
                << " * AUTOGENERATED by mrustc\n"
                << " */\n"
                << "#include <stddef.h>\n"
                << "#include <stdint.h>\n"
                << "#include <stdbool.h>\n"
                << "typedef uint32_t CHAR;\n"
                << "typedef struct { } tUNIT;\n"
                << "typedef struct { } tBANG;\n"
                << "typedef struct { char* PTR; size_t META; } STR_PTR;\n"
                << "typedef struct { void* PTR; size_t META; } SLICE_PTR;\n"
                << "typedef struct { void* PTR; void* META; } TRAITOBJ_PTR;\n"
                << "\n";
        }
        
        ~CodeGenerator_C() {}

        void finalise() override
        {
        }
        
        void emit_type(const ::HIR::TypeRef& ty) override
        {
            TU_IFLET( ::HIR::TypeRef::Data, ty.m_data, Tuple, te,
                if( te.size() > 0 )
                {
                    m_of << "typedef struct {\n";
                    for(unsigned int i = 0; i < te.size(); i++)
                    {
                        m_of << "\t";
                        emit_ctype(te[i], FMT_CB(ss, ss << "_" << i;));
                        m_of << ";\n";
                    }
                    // TODO: Fields.
                    m_of << "} "; emit_ctype(ty); m_of << ";\n";
                }
            )
            else TU_IFLET( ::HIR::TypeRef::Data, ty.m_data, Function, te,
                m_of << "typedef ";
                // TODO: ABI marker, need an ABI enum?
                // TODO: Better emit_ctype call for return type.
                emit_ctype(*te.m_rettype); m_of << " (*"; emit_ctype(ty); m_of << ")(";
                if( te.m_arg_types.size() == 0 )
                {
                    m_of << "void)";
                }
                else
                {
                    for(unsigned int i = 0; i < te.m_arg_types.size(); i ++)
                    {
                        if(i != 0)  m_of << ",";
                        m_of << " ";
                        emit_ctype(te.m_arg_types[i]);
                    }
                    m_of << " )";
                }
                m_of << ";\n";
            )
            else {
            }
        }
        
        void emit_struct(const Span& sp, const ::HIR::GenericPath& p, const ::HIR::Struct& item) override
        {
            ::HIR::TypeRef  tmp;
            auto monomorph = [&](const auto& x)->const auto& {
                if( monomorphise_type_needed(x) ) {
                    tmp = monomorphise_type(sp, item.m_params, p.m_params, x);
                    m_resolve.expand_associated_types(sp, tmp);
                    return tmp;
                }
                else {
                    return x;
                }
                };
            auto emit_struct_fld_ty = [&](const ::HIR::TypeRef& ty, ::FmtLambda inner) {
                TU_IFLET(::HIR::TypeRef::Data, ty.m_data, Slice, te,
                    emit_ctype( monomorph(*te.inner), FMT_CB(ss, ss << inner << "[]";) );
                )
                else {
                    emit_ctype( monomorph(ty), inner );
                }
                };
            m_of << "struct s_" << Trans_Mangle(p) << " {\n";
            TU_MATCHA( (item.m_data), (e),
            (Unit,
                ),
            (Tuple,
                for(unsigned int i = 0; i < e.size(); i ++)
                {
                    const auto& fld = e[i];
                    m_of << "\t";
                    emit_struct_fld_ty(fld.ent, FMT_CB(ss, ss << "_" << i;));
                    m_of << ";\n";
                }
                ),
            (Named,
                for(unsigned int i = 0; i < e.size(); i ++)
                {
                    const auto& fld = e[i].second;
                    m_of << "\t";
                    emit_struct_fld_ty(fld.ent, FMT_CB(ss, ss << "_" << i;));
                    m_of << ";\n";
                }
                )
            )
            m_of << "};\n";
        }
        //virtual void emit_union(const ::HIR::GenericPath& p, const ::HIR::Union& item);
        void emit_enum(const Span& sp, const ::HIR::GenericPath& p, const ::HIR::Enum& item) override
        {
            ::HIR::TypeRef  tmp;
            auto monomorph = [&](const auto& x)->const auto& {
                if( monomorphise_type_needed(x) ) {
                    tmp = monomorphise_type(sp, item.m_params, p.m_params, x);
                    m_resolve.expand_associated_types(sp, tmp);
                    return tmp;
                }
                else {
                    return x;
                }
                };
            m_of << "struct e_" << Trans_Mangle(p) << " {\n";
            m_of << "\tunsigned int TAG;\n";
            m_of << "\tunion {\n";
            for(unsigned int i = 0; i < item.m_variants.size(); i ++)
            {
                m_of << "\t\tstruct {\n";
                TU_MATCHA( (item.m_variants[i].second), (e),
                (Unit,
                    ),
                (Value,
                    // TODO: omit
                    ),
                (Tuple,
                    for(unsigned int i = 0; i < e.size(); i ++)
                    {
                        const auto& fld = e[i];
                        m_of << "\t\t\t";
                        emit_ctype( monomorph(fld.ent) );
                        m_of << " _" << i << ";\n";
                    }
                    ),
                (Struct,
                    for(unsigned int i = 0; i < e.size(); i ++)
                    {
                        const auto& fld = e[i];
                        m_of << "\t\t\t";
                        emit_ctype( monomorph(fld.second.ent) );
                        m_of << " _" << i << ";\n";
                    }
                    )
                )
                m_of << "\t\t} var_" << i << ";\n";
            }
            m_of << "\t} DATA;\n";
            m_of << "};\n";
        }
        
        //virtual void emit_static_ext(const ::HIR::Path& p);
        //virtual void emit_static_local(const ::HIR::Path& p, const ::HIR::Static& item, const Trans_Params& params);
        
        void emit_function_ext(const ::HIR::Path& p, const ::HIR::Function& item, const Trans_Params& params) override
        {
            m_of << "extern ";
            emit_function_header(p, item, params);
            m_of << ";\n";
        }
        void emit_function_proto(const ::HIR::Path& p, const ::HIR::Function& item, const Trans_Params& params) override
        {
            emit_function_header(p, item, params);
            m_of << ";\n";
        }
        void emit_function_code(const ::HIR::Path& p, const ::HIR::Function& item, const Trans_Params& params, const ::MIR::FunctionPointer& code) override
        {
            static Span sp;
            TRACE_FUNCTION_F(p);
            
            ::MIR::TypeResolve::args_t  arg_types;
            for(const auto& ent : item.m_args)
                arg_types.push_back(::std::make_pair( ::HIR::Pattern{}, params.monomorph(m_crate, ent.second) ));
            ::HIR::TypeRef  ret_type = params.monomorph(m_crate, item.m_return);
            
            ::MIR::TypeResolve  mir_res { sp, m_crate, ret_type, arg_types, *code };
            m_mir_res = &mir_res;

            m_of << "// " << p << "\n";
            emit_function_header(p, item, params);
            m_of << "\n";
            m_of << "{\n";
            // Variables
            m_of << "\t"; emit_ctype(params.monomorph(m_crate, item.m_return)); m_of << " rv;\n";
            for(unsigned int i = 0; i < code->named_variables.size(); i ++) {
                DEBUG("var" << i << " : " << code->named_variables[i]);
                m_of << "\t"; emit_ctype(code->named_variables[i], FMT_CB(ss, ss << "var" << i;)); m_of << ";";
                m_of << "\t// " << code->named_variables[i];
                m_of << "\n";
            }
            for(unsigned int i = 0; i < code->temporaries.size(); i ++) {
                DEBUG("tmp" << i << " : " << code->temporaries[i]);
                m_of << "\t"; emit_ctype(code->temporaries[i], FMT_CB(ss, ss << " tmp" << i;)); m_of << ";";
                m_of << "\t// " << code->temporaries[i];
                m_of << "\n";
            }
            // TODO: Code.
            for(unsigned int i = 0; i < code->blocks.size(); i ++)
            {
                TRACE_FUNCTION_F(p << " bb" << i);
            
                m_of << "bb" << i << ":\n";
                
                for(const auto& stmt : code->blocks[i].statements)
                {
                    assert( stmt.is_Drop() || stmt.is_Assign() );
                    if( stmt.is_Drop() ) {
                    }
                    else {
                        const auto& e = stmt.as_Assign();
                        DEBUG("- " << e.dst << " = " << e.src);
                        m_of << "\t";
                        TU_MATCHA( (e.src), (ve),
                        (Use,
                            emit_lvalue(e.dst);
                            m_of << " = ";
                            emit_lvalue(ve);
                            ),
                        (Constant,
                            TU_MATCHA( (ve), (c),
                            (Int,
                                emit_lvalue(e.dst);
                                m_of << " = ";
                                m_of << c;
                                ),
                            (Uint,
                                emit_lvalue(e.dst);
                                m_of << " = ";
                                m_of << ::std::hex << "0x" << c << ::std::dec;
                                ),
                            (Float,
                                emit_lvalue(e.dst);
                                m_of << " = ";
                                m_of << c;
                                ),
                            (Bool,
                                emit_lvalue(e.dst);
                                m_of << " = ";
                                m_of << (c ? "true" : "false");
                                ),
                            // TODO: These need to be arrays, not strings! (strings are NUL terminated)
                            (Bytes,
                                emit_lvalue(e.dst);
                                m_of << ".PTR = ";
                                m_of << "\"" << ::std::oct;
                                for(const auto& v : c) {
                                    if( ' ' <= v && v < 0x7F && v != '"' && v != '\\' )
                                        m_of << v;
                                    else
                                        m_of << "\\" << (unsigned int)v;
                                }
                                m_of << "\"" << ::std::dec;
                                m_of << ";\n\t";
                                emit_lvalue(e.dst);
                                m_of << ".META = " << c.size();
                                ),
                            (StaticString,
                                emit_lvalue(e.dst);
                                m_of << ".PTR = ";
                                m_of << "\"" << ::std::oct;
                                for(const auto& v : c) {
                                    if( ' ' <= v && v < 0x7F && v != '"' && v != '\\' )
                                        m_of << v;
                                    else
                                        m_of << "\\" << (unsigned int)v;
                                }
                                m_of << "\"" << ::std::dec;
                                
                                m_of << ";\n\t";
                                emit_lvalue(e.dst);
                                m_of << ".META = " << c.size();
                                ),
                            (Const,
                                // TODO: This should have been eliminated?
                                emit_lvalue(e.dst);
                                m_of << " = /*CONST*/";
                                ),
                            (ItemAddr,
                                emit_lvalue(e.dst);
                                m_of << " = ";
                                m_of << "&" << Trans_Mangle(c);
                                )
                            )
                            ),
                        (SizedArray,
                            emit_lvalue(e.dst);
                            m_of << " = ";
                            m_of << "{";
                            for(unsigned int j = ve.count; j --;) {
                                emit_lvalue(ve.val);
                                if( j != 0 )    m_of << ",";
                            }
                            m_of << "}";
                            ),
                        (Borrow,
                            emit_lvalue(e.dst);
                            m_of << " = ";
                            bool special = false;
                            // If the inner value has type [T] or str, just assign.
                            TU_IFLET(::MIR::LValue, ve.val, Deref, e,
                                ::HIR::TypeRef  tmp;
                                const auto& ty = mir_res.get_lvalue_type(tmp, ve.val);  // NOTE: Checks the result of the deref
                                if( is_dst(ty) ) {
                                    emit_lvalue(*e.val);
                                    special = true;
                                }
                            )
                            if( !special )
                            {
                                m_of << "& "; emit_lvalue(ve.val);
                            }
                            ),
                        (Cast,
                            emit_lvalue(e.dst);
                            m_of << " = ";
                            m_of << "("; emit_ctype(ve.type); m_of << ")";
                            // TODO: If the source is an unsized borrow, then extract the pointer
                            bool special = false;
                            if( ve.type.m_data.is_Pointer() && !is_dst( *ve.type.m_data.as_Pointer().inner ) )
                            {
                                ::HIR::TypeRef  tmp;
                                const auto& ty = mir_res.get_lvalue_type(tmp, ve.val);  // NOTE: Checks the result of the deref
                                if( (ty.m_data.is_Borrow() && is_dst(*ty.m_data.as_Borrow().inner))
                                 || (ty.m_data.is_Pointer() && is_dst(*ty.m_data.as_Pointer().inner))
                                    )
                                {
                                    emit_lvalue(ve.val);
                                    m_of << ".PTR";
                                    special = true;
                                }
                            }
                            if( !special )
                            {
                                emit_lvalue(ve.val);
                            }
                            ),
                        (BinOp,
                            emit_lvalue(e.dst);
                            m_of << " = ";
                            emit_lvalue(ve.val_l);
                            switch(ve.op)
                            {
                            case ::MIR::eBinOp::ADD:   m_of << " + ";    break;
                            case ::MIR::eBinOp::SUB:   m_of << " - ";    break;
                            case ::MIR::eBinOp::MUL:   m_of << " * ";    break;
                            case ::MIR::eBinOp::DIV:   m_of << " / ";    break;
                            case ::MIR::eBinOp::MOD:   m_of << " % ";    break;
                            
                            case ::MIR::eBinOp::BIT_OR:    m_of << " | ";    break;
                            case ::MIR::eBinOp::BIT_AND:   m_of << " & ";    break;
                            case ::MIR::eBinOp::BIT_XOR:   m_of << " ^ ";    break;
                            case ::MIR::eBinOp::BIT_SHR:   m_of << " >> ";   break;
                            case ::MIR::eBinOp::BIT_SHL:   m_of << " << ";   break;
                            case ::MIR::eBinOp::EQ:    m_of << " == ";   break;
                            case ::MIR::eBinOp::NE:    m_of << " != ";   break;
                            case ::MIR::eBinOp::GT:    m_of << " > " ;   break;
                            case ::MIR::eBinOp::GE:    m_of << " >= ";   break;
                            case ::MIR::eBinOp::LT:    m_of << " < " ;   break;
                            case ::MIR::eBinOp::LE:    m_of << " <= ";   break;
                            
                            case ::MIR::eBinOp::ADD_OV:
                            case ::MIR::eBinOp::SUB_OV:
                            case ::MIR::eBinOp::MUL_OV:
                            case ::MIR::eBinOp::DIV_OV:
                                TODO(sp, "Overflow");
                                break;
                            }
                            emit_lvalue(ve.val_r);
                            ),
                        (UniOp,
                            emit_lvalue(e.dst);
                            m_of << " = ";
                            switch(ve.op)
                            {
                            case ::MIR::eUniOp::NEG:    m_of << "-";    break;
                            case ::MIR::eUniOp::INV:    m_of << "~";    break;
                            }
                            emit_lvalue(ve.val);
                            ),
                        (DstMeta,
                            emit_lvalue(e.dst);
                            m_of << " = ";
                            emit_lvalue(ve.val);
                            m_of << ".META";
                            ),
                        (DstPtr,
                            emit_lvalue(e.dst);
                            m_of << " = ";
                            emit_lvalue(ve.val);
                            m_of << ".PTR";
                            ),
                        (MakeDst,
                            emit_lvalue(e.dst);
                            m_of << ".PTR = ";
                            emit_lvalue(ve.ptr_val);
                            m_of << ";\n\t";
                            emit_lvalue(e.dst);
                            m_of << ".META = ";
                            emit_lvalue(ve.meta_val);
                            ),
                        (Tuple,
                            for(unsigned int j = 0; j < ve.vals.size(); j ++) {
                                if( j != 0 )    m_of << ";\n\t";
                                emit_lvalue(e.dst);
                                m_of << "._" << j << " = ";
                                emit_lvalue(ve.vals[j]);
                            }
                            ),
                        (Array,
                            m_of << "{";
                            for(unsigned int j = 0; j < ve.vals.size(); j ++) {
                                if( j != 0 )    m_of << ",";
                                emit_lvalue(ve.vals[j]);
                            }
                            m_of << "}";
                            ),
                        (Variant,
                            TODO(sp, "Handle constructing variants");
                            ),
                        (Struct,
                            for(unsigned int j = 0; j < ve.vals.size(); j ++) {
                                if( j != 0 )    m_of << ";\n\t";
                                emit_lvalue(e.dst);
                                if(ve.variant_idx != ~0u)
                                    m_of << ".DATA.var_" << ve.variant_idx;
                                m_of << "._" << j << " = ";
                                emit_lvalue(ve.vals[j]);
                            }
                            )
                        )
                        m_of << ";";
                        m_of << "\t// " << e.dst << " = " << e.src;
                        m_of << "\n";
                    }
                }
                TU_MATCHA( (code->blocks[i].terminator), (e),
                (Incomplete,
                    m_of << "\tfor(;;);\n";
                    ),
                (Return,
                    m_of << "\treturn rv;\n";
                    ),
                (Diverge,
                    m_of << "\t_Unwind_Resume();\n";
                    ),
                (Goto,
                    m_of << "\tgoto bb" << e << ";\n";
                    ),
                (Panic,
                    m_of << "\tgoto bb" << e << "; /* panic */\n";
                    ),
                (If,
                    m_of << "\tif("; emit_lvalue(e.cond); m_of << ") goto bb" << e.bb0 << "; else goto bb" << e.bb1 << ";\n";
                    ),
                (Switch,
                    m_of << "\tswitch("; emit_lvalue(e.val); m_of << ".TAG) {\n";
                    for(unsigned int j = 0; j < e.targets.size(); j ++)
                        m_of << "\t\tcase " << j << ": goto bb" << e.targets[j] << ";\n";
                    m_of << "\t}\n";
                    ),
                (CallValue,
                    m_of << "\t"; emit_lvalue(e.ret_val); m_of << " = ("; emit_lvalue(e.fcn_val); m_of << ")(";
                    for(unsigned int j = 0; j < e.args.size(); j ++) {
                        if(j != 0)  m_of << ",";
                        m_of << " "; emit_lvalue(e.args[j]);
                    }
                    m_of << " );\n";
                    m_of << "\tgoto bb" << e.ret_block << ";\n";
                    ),
                (CallPath,
                    m_of << "\t"; emit_lvalue(e.ret_val); m_of << " = " << Trans_Mangle(e.fcn_path) << "(";
                    for(unsigned int j = 0; j < e.args.size(); j ++) {
                        if(j != 0)  m_of << ",";
                        m_of << " "; emit_lvalue(e.args[j]);
                    }
                    m_of << " );\n";
                    m_of << "\tgoto bb" << e.ret_block << ";\n";
                    )
                )
            }
            m_of << "}\n";
            m_of.flush();
            m_mir_res = nullptr;
        }
    private:
        void emit_function_header(const ::HIR::Path& p, const ::HIR::Function& item, const Trans_Params& params)
        {
            emit_ctype( params.monomorph(m_crate, item.m_return) );
            m_of << " " << Trans_Mangle(p) << "(";
            if( item.m_args.size() == 0 )
            {
                m_of << "void)";
            }
            else
            {
                for(unsigned int i = 0; i < item.m_args.size(); i ++)
                {
                    if( i != 0 )    m_of << ",";
                    m_of << "\n\t\t";
                    emit_ctype( params.monomorph(m_crate, item.m_args[i].second) );
                    m_of << " arg" << i;
                }
                m_of << "\n\t\t)";
            }
        }
        void emit_lvalue(const ::MIR::LValue& val) {
            TU_MATCHA( (val), (e),
            (Variable,
                m_of << "var" << e;
                ),
            (Temporary,
                m_of << "tmp" << e.idx;
                ),
            (Argument,
                m_of << "arg" << e.idx;
                ),
            (Return,
                m_of << "rv";
                ),
            (Static,
                m_of << Trans_Mangle(e);
                ),
            (Field,
                // TODO: Also used for indexing
                emit_lvalue(*e.val);
                m_of << "._" << e.field_index;
                ),
            (Deref,
                m_of << "(*";
                emit_lvalue(*e.val);
                m_of << ")";
                ),
            (Index,
                ::HIR::TypeRef  tmp;
                const auto& ty = m_mir_res->get_lvalue_type(tmp, *e.val);
                m_of << "(";
                if( ty.m_data.is_Slice() ) {
                    assert(e.val->is_Deref());
                    m_of << "("; emit_ctype(*ty.m_data.as_Slice().inner); m_of << "*)";
                    emit_lvalue(*e.val->as_Deref().val);
                    m_of << ".PTR";
                }
                else {
                    emit_lvalue(*e.val);
                }
                m_of << ")[";
                emit_lvalue(*e.idx);
                m_of << "]";
                ),
            (Downcast,
                emit_lvalue(*e.val);
                m_of << ".DATA.var_" << e.variant_index;
                )
            )
        }
        void emit_ctype(const ::HIR::TypeRef& ty) {
            emit_ctype(ty, FMT_CB(_,));
        }
        void emit_ctype(const ::HIR::TypeRef& ty, ::FmtLambda inner) {
            TU_MATCHA( (ty.m_data), (te),
            (Infer,
                m_of << "@" << ty << "@" << inner;
                ),
            (Diverge,
                m_of << "tBANG " << inner;
                ),
            (Primitive,
                switch(te)
                {
                case ::HIR::CoreType::Usize:    m_of << "uintptr_t";   break;
                case ::HIR::CoreType::Isize:    m_of << "intptr_t";  break;
                case ::HIR::CoreType::U8:  m_of << "uint8_t"; break;
                case ::HIR::CoreType::I8:  m_of << "int8_t"; break;
                case ::HIR::CoreType::U16: m_of << "uint16_t"; break;
                case ::HIR::CoreType::I16: m_of << "int16_t"; break;
                case ::HIR::CoreType::U32: m_of << "uint32_t"; break;
                case ::HIR::CoreType::I32: m_of << "int32_t"; break;
                case ::HIR::CoreType::U64: m_of << "uint64_t"; break;
                case ::HIR::CoreType::I64: m_of << "int64_t"; break;
                
                case ::HIR::CoreType::F32: m_of << "float"; break;
                case ::HIR::CoreType::F64: m_of << "double"; break;
                
                case ::HIR::CoreType::Bool: m_of << "bool"; break;
                case ::HIR::CoreType::Char: m_of << "CHAR";  break;
                case ::HIR::CoreType::Str:
                    BUG(Span(), "Raw str");
                }
                m_of << " " << inner;
                ),
            (Path,
                TU_MATCHA( (te.binding), (tpb),
                (Struct,
                    m_of << "struct s_" << Trans_Mangle(te.path);
                    ),
                (Union,
                    m_of << "union u_" << Trans_Mangle(te.path);
                    ),
                (Enum,
                    m_of << "struct e_" << Trans_Mangle(te.path);
                    ),
                (Unbound,
                    BUG(Span(), "Unbound path in trans - " << ty);
                    ),
                (Opaque,
                    BUG(Span(), "Opaque path in trans - " << ty);
                    )
                )
                m_of << " " << inner;
                ),
            (Generic,
                BUG(Span(), "Generic in trans - " << ty);
                ),
            (TraitObject,
                BUG(Span(), "Raw trait object - " << ty);
                ),
            (ErasedType,
                BUG(Span(), "ErasedType in trans - " << ty);
                ),
            (Array,
                emit_ctype(*te.inner, inner);
                m_of << "[" << te.size_val << "]";
                ),
            (Slice,
                BUG(Span(), "Raw slice object - " << ty);
                ),
            (Tuple,
                if( te.size() == 0 )
                    m_of << "tUNIT";
                else {
                    m_of << "TUP_" << te.size();
                    for(const auto& t : te)
                        m_of << "_" << Trans_Mangle(t);
                }
                m_of << " " << inner;
                ),
            (Borrow,
                emit_ctype_ptr(*te.inner, inner);
                ),
            (Pointer,
                emit_ctype_ptr(*te.inner, inner);
                ),
            (Function,
                m_of << "t_" << Trans_Mangle(ty) << " " << inner;
                ),
            (Closure,
                BUG(Span(), "Closure during trans - " << ty);
                )
            )
        }
        
        void emit_ctype_ptr(const ::HIR::TypeRef& inner_ty, ::FmtLambda inner) {
            if( inner_ty == ::HIR::CoreType::Str ) {
                m_of << "STR_PTR " << inner;
            }
            else if( inner_ty.m_data.is_TraitObject() ) {
                m_of << "TRAITOBJ_PTR " << inner;
            }
            else if( inner_ty.m_data.is_Slice() ) {
                m_of << "SLICE_PTR " << inner;
            }
            else if( inner_ty.m_data.is_Array() ) {
                emit_ctype(inner_ty, FMT_CB(ss, ss << "(*" << inner << ")";));
            }
            else {
                emit_ctype(inner_ty, FMT_CB(ss, ss << "*" << inner;));
            }
        }
        
        int is_dst(const ::HIR::TypeRef& ty) const
        {
            if( ty == ::HIR::CoreType::Str )
                return 1;
            if( ty.m_data.is_Slice() )
                return 1;
            if( ty.m_data.is_TraitObject() )
                return 2;
            // TODO: Unsized named types.
            return 0;
        }
    };
}

::std::unique_ptr<CodeGenerator> Trans_Codegen_GetGeneratorC(const ::HIR::Crate& crate, const ::std::string& outfile)
{
    return ::std::unique_ptr<CodeGenerator>(new CodeGenerator_C(crate, outfile));
}
