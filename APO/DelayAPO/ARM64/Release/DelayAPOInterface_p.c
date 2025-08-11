

/* this ALWAYS GENERATED file contains the proxy stub code */


 /* File created by MIDL compiler version 8.01.0628 */
/* Compiler settings for DelayAPOInterface.idl:
    Oicf, W1, Zp8, env=Win64 (32b run), target_arch=ARM64 8.01.0628 
    protocol : all , ms_ext, c_ext, robust
    error checks: allocation ref bounds_check enum stub_data 
    VC __declspec() decoration level: 
         __declspec(uuid()), __declspec(selectany), __declspec(novtable)
         DECLSPEC_UUID(), MIDL_INTERFACE()
*/
/* @@MIDL_FILE_HEADING(  ) */

#if defined(_M_ARM64)


#if _MSC_VER >= 1200
#pragma warning(push)
#endif

#pragma warning( disable: 4211 )  /* redefine extern to static */
#pragma warning( disable: 4232 )  /* dllimport identity*/
#pragma warning( disable: 4024 )  /* array to pointer mapping*/
#pragma warning( disable: 4152 )  /* function/data pointer conversion in expression */

#define USE_STUBLESS_PROXY


/* verify that the <rpcproxy.h> version is high enough to compile this file*/
#ifndef __REDQ_RPCPROXY_H_VERSION__
#define __REQUIRED_RPCPROXY_H_VERSION__ 475
#endif


#include "rpcproxy.h"
#include "ndr64types.h"
#ifndef __RPCPROXY_H_VERSION__
#error this stub requires an updated version of <rpcproxy.h>
#endif /* __RPCPROXY_H_VERSION__ */


#include "DelayAPOInterface.h"

#define TYPE_FORMAT_STRING_SIZE   3                                 
#define PROC_FORMAT_STRING_SIZE   1                                 
#define EXPR_FORMAT_STRING_SIZE   1                                 
#define TRANSMIT_AS_TABLE_SIZE    0            
#define WIRE_MARSHAL_TABLE_SIZE   0            

typedef struct _DelayAPOInterface_MIDL_TYPE_FORMAT_STRING
    {
    short          Pad;
    unsigned char  Format[ TYPE_FORMAT_STRING_SIZE ];
    } DelayAPOInterface_MIDL_TYPE_FORMAT_STRING;

typedef struct _DelayAPOInterface_MIDL_PROC_FORMAT_STRING
    {
    short          Pad;
    unsigned char  Format[ PROC_FORMAT_STRING_SIZE ];
    } DelayAPOInterface_MIDL_PROC_FORMAT_STRING;

typedef struct _DelayAPOInterface_MIDL_EXPR_FORMAT_STRING
    {
    long          Pad;
    unsigned char  Format[ EXPR_FORMAT_STRING_SIZE ];
    } DelayAPOInterface_MIDL_EXPR_FORMAT_STRING;


static const RPC_SYNTAX_IDENTIFIER  _RpcTransferSyntax_2_0 = 
{{0x8A885D04,0x1CEB,0x11C9,{0x9F,0xE8,0x08,0x00,0x2B,0x10,0x48,0x60}},{2,0}};

static const RPC_SYNTAX_IDENTIFIER  _NDR64_RpcTransferSyntax_1_0 = 
{{0x71710533,0xbeba,0x4937,{0x83,0x19,0xb5,0xdb,0xef,0x9c,0xcc,0x36}},{1,0}};

#if defined(_CONTROL_FLOW_GUARD_XFG)
#define XFG_TRAMPOLINES(ObjectType)\
NDR_SHAREABLE unsigned long ObjectType ## _UserSize_XFG(unsigned long * pFlags, unsigned long Offset, void * pObject)\
{\
return  ObjectType ## _UserSize(pFlags, Offset, (ObjectType *)pObject);\
}\
NDR_SHAREABLE unsigned char * ObjectType ## _UserMarshal_XFG(unsigned long * pFlags, unsigned char * pBuffer, void * pObject)\
{\
return ObjectType ## _UserMarshal(pFlags, pBuffer, (ObjectType *)pObject);\
}\
NDR_SHAREABLE unsigned char * ObjectType ## _UserUnmarshal_XFG(unsigned long * pFlags, unsigned char * pBuffer, void * pObject)\
{\
return ObjectType ## _UserUnmarshal(pFlags, pBuffer, (ObjectType *)pObject);\
}\
NDR_SHAREABLE void ObjectType ## _UserFree_XFG(unsigned long * pFlags, void * pObject)\
{\
ObjectType ## _UserFree(pFlags, (ObjectType *)pObject);\
}
#define XFG_TRAMPOLINES64(ObjectType)\
NDR_SHAREABLE unsigned long ObjectType ## _UserSize64_XFG(unsigned long * pFlags, unsigned long Offset, void * pObject)\
{\
return  ObjectType ## _UserSize64(pFlags, Offset, (ObjectType *)pObject);\
}\
NDR_SHAREABLE unsigned char * ObjectType ## _UserMarshal64_XFG(unsigned long * pFlags, unsigned char * pBuffer, void * pObject)\
{\
return ObjectType ## _UserMarshal64(pFlags, pBuffer, (ObjectType *)pObject);\
}\
NDR_SHAREABLE unsigned char * ObjectType ## _UserUnmarshal64_XFG(unsigned long * pFlags, unsigned char * pBuffer, void * pObject)\
{\
return ObjectType ## _UserUnmarshal64(pFlags, pBuffer, (ObjectType *)pObject);\
}\
NDR_SHAREABLE void ObjectType ## _UserFree64_XFG(unsigned long * pFlags, void * pObject)\
{\
ObjectType ## _UserFree64(pFlags, (ObjectType *)pObject);\
}
#define XFG_BIND_TRAMPOLINES(HandleType, ObjectType)\
static void* ObjectType ## _bind_XFG(HandleType pObject)\
{\
return ObjectType ## _bind((ObjectType) pObject);\
}\
static void ObjectType ## _unbind_XFG(HandleType pObject, handle_t ServerHandle)\
{\
ObjectType ## _unbind((ObjectType) pObject, ServerHandle);\
}
#define XFG_TRAMPOLINE_FPTR(Function) Function ## _XFG
#define XFG_TRAMPOLINE_FPTR_DEPENDENT_SYMBOL(Symbol) Symbol ## _XFG
#else
#define XFG_TRAMPOLINES(ObjectType)
#define XFG_TRAMPOLINES64(ObjectType)
#define XFG_BIND_TRAMPOLINES(HandleType, ObjectType)
#define XFG_TRAMPOLINE_FPTR(Function) Function
#define XFG_TRAMPOLINE_FPTR_DEPENDENT_SYMBOL(Symbol) Symbol
#endif



extern const DelayAPOInterface_MIDL_TYPE_FORMAT_STRING DelayAPOInterface__MIDL_TypeFormatString;
extern const DelayAPOInterface_MIDL_PROC_FORMAT_STRING DelayAPOInterface__MIDL_ProcFormatString;
extern const DelayAPOInterface_MIDL_EXPR_FORMAT_STRING DelayAPOInterface__MIDL_ExprFormatString;

#ifdef __cplusplus
namespace {
#endif

extern const MIDL_STUB_DESC Object_StubDesc;
#ifdef __cplusplus
}
#endif


extern const MIDL_SERVER_INFO IDelayAPOMFX_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO IDelayAPOMFX_ProxyInfo;

#ifdef __cplusplus
namespace {
#endif

extern const MIDL_STUB_DESC Object_StubDesc;
#ifdef __cplusplus
}
#endif


extern const MIDL_SERVER_INFO IDelayAPOSFX_ServerInfo;
extern const MIDL_STUBLESS_PROXY_INFO IDelayAPOSFX_ProxyInfo;



#if !defined(__RPC_ARM64__)
#error  Invalid build platform for this stub.
#endif

static const DelayAPOInterface_MIDL_PROC_FORMAT_STRING DelayAPOInterface__MIDL_ProcFormatString =
    {
        0,
        {

			0x0
        }
    };

static const DelayAPOInterface_MIDL_TYPE_FORMAT_STRING DelayAPOInterface__MIDL_TypeFormatString =
    {
        0,
        {
			NdrFcShort( 0x0 ),	/* 0 */

			0x0
        }
    };


/* Object interface: IUnknown, ver. 0.0,
   GUID={0x00000000,0x0000,0x0000,{0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}} */


/* Object interface: IDelayAPOMFX, ver. 0.0,
   GUID={0x4c0531b4,0xf2ca,0x4eae,{0x80,0xcd,0x02,0x02,0x66,0x92,0x3b,0xc1}} */

#pragma code_seg(".orpc")
static const unsigned short IDelayAPOMFX_FormatStringOffsetTable[] =
    {
    0
    };



/* Object interface: IDelayAPOSFX, ver. 0.0,
   GUID={0x6269b338,0x5162,0x4f14,{0xa9,0x4d,0x84,0x22,0xee,0x93,0xec,0x13}} */

#pragma code_seg(".orpc")
static const unsigned short IDelayAPOSFX_FormatStringOffsetTable[] =
    {
    0
    };



#endif /* defined(_M_ARM64) */



/* this ALWAYS GENERATED file contains the proxy stub code */


 /* File created by MIDL compiler version 8.01.0628 */
/* Compiler settings for DelayAPOInterface.idl:
    Oicf, W1, Zp8, env=Win64 (32b run), target_arch=ARM64 8.01.0628 
    protocol : all , ms_ext, c_ext, robust
    error checks: allocation ref bounds_check enum stub_data 
    VC __declspec() decoration level: 
         __declspec(uuid()), __declspec(selectany), __declspec(novtable)
         DECLSPEC_UUID(), MIDL_INTERFACE()
*/
/* @@MIDL_FILE_HEADING(  ) */

#if defined(_M_ARM64)




#if !defined(__RPC_ARM64__)
#error  Invalid build platform for this stub.
#endif


#include "ndr64types.h"
#include "pshpack8.h"
#ifdef __cplusplus
namespace {
#endif


typedef 
NDR64_FORMAT_UINT32
__midl_frag1_t;
extern const __midl_frag1_t __midl_frag1;

static const __midl_frag1_t __midl_frag1 =
(NDR64_UINT32) 0 /* 0x0 */;
#ifdef __cplusplus
}
#endif


#include "poppack.h"



/* Object interface: IUnknown, ver. 0.0,
   GUID={0x00000000,0x0000,0x0000,{0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}} */


/* Object interface: IDelayAPOMFX, ver. 0.0,
   GUID={0x4c0531b4,0xf2ca,0x4eae,{0x80,0xcd,0x02,0x02,0x66,0x92,0x3b,0xc1}} */

#pragma code_seg(".orpc")
static const FormatInfoRef IDelayAPOMFX_Ndr64ProcTable[] =
    {
    0
    };


static const MIDL_SYNTAX_INFO IDelayAPOMFX_SyntaxInfo [  2 ] = 
    {
    {
    {{0x8A885D04,0x1CEB,0x11C9,{0x9F,0xE8,0x08,0x00,0x2B,0x10,0x48,0x60}},{2,0}},
    0,
    DelayAPOInterface__MIDL_ProcFormatString.Format,
    &IDelayAPOMFX_FormatStringOffsetTable[-3],
    DelayAPOInterface__MIDL_TypeFormatString.Format,
    0,
    0,
    0
    }
    ,{
    {{0x71710533,0xbeba,0x4937,{0x83,0x19,0xb5,0xdb,0xef,0x9c,0xcc,0x36}},{1,0}},
    0,
    0 ,
    (unsigned short *) &IDelayAPOMFX_Ndr64ProcTable[-3],
    0,
    0,
    0,
    0
    }
    };

static const MIDL_STUBLESS_PROXY_INFO IDelayAPOMFX_ProxyInfo =
    {
    &Object_StubDesc,
    DelayAPOInterface__MIDL_ProcFormatString.Format,
    &IDelayAPOMFX_FormatStringOffsetTable[-3],
    (RPC_SYNTAX_IDENTIFIER*)&_RpcTransferSyntax_2_0,
    2,
    (MIDL_SYNTAX_INFO*)IDelayAPOMFX_SyntaxInfo
    
    };


static const MIDL_SERVER_INFO IDelayAPOMFX_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    DelayAPOInterface__MIDL_ProcFormatString.Format,
    (unsigned short *) &IDelayAPOMFX_FormatStringOffsetTable[-3],
    0,
    (RPC_SYNTAX_IDENTIFIER*)&_NDR64_RpcTransferSyntax_1_0,
    2,
    (MIDL_SYNTAX_INFO*)IDelayAPOMFX_SyntaxInfo
    };
CINTERFACE_PROXY_VTABLE(3) _IDelayAPOMFXProxyVtbl = 
{
    0,
    &IID_IDelayAPOMFX,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy
};

const CInterfaceStubVtbl _IDelayAPOMFXStubVtbl =
{
    &IID_IDelayAPOMFX,
    &IDelayAPOMFX_ServerInfo,
    3,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS
};


/* Object interface: IDelayAPOSFX, ver. 0.0,
   GUID={0x6269b338,0x5162,0x4f14,{0xa9,0x4d,0x84,0x22,0xee,0x93,0xec,0x13}} */

#pragma code_seg(".orpc")
static const FormatInfoRef IDelayAPOSFX_Ndr64ProcTable[] =
    {
    0
    };


static const MIDL_SYNTAX_INFO IDelayAPOSFX_SyntaxInfo [  2 ] = 
    {
    {
    {{0x8A885D04,0x1CEB,0x11C9,{0x9F,0xE8,0x08,0x00,0x2B,0x10,0x48,0x60}},{2,0}},
    0,
    DelayAPOInterface__MIDL_ProcFormatString.Format,
    &IDelayAPOSFX_FormatStringOffsetTable[-3],
    DelayAPOInterface__MIDL_TypeFormatString.Format,
    0,
    0,
    0
    }
    ,{
    {{0x71710533,0xbeba,0x4937,{0x83,0x19,0xb5,0xdb,0xef,0x9c,0xcc,0x36}},{1,0}},
    0,
    0 ,
    (unsigned short *) &IDelayAPOSFX_Ndr64ProcTable[-3],
    0,
    0,
    0,
    0
    }
    };

static const MIDL_STUBLESS_PROXY_INFO IDelayAPOSFX_ProxyInfo =
    {
    &Object_StubDesc,
    DelayAPOInterface__MIDL_ProcFormatString.Format,
    &IDelayAPOSFX_FormatStringOffsetTable[-3],
    (RPC_SYNTAX_IDENTIFIER*)&_RpcTransferSyntax_2_0,
    2,
    (MIDL_SYNTAX_INFO*)IDelayAPOSFX_SyntaxInfo
    
    };


static const MIDL_SERVER_INFO IDelayAPOSFX_ServerInfo = 
    {
    &Object_StubDesc,
    0,
    DelayAPOInterface__MIDL_ProcFormatString.Format,
    (unsigned short *) &IDelayAPOSFX_FormatStringOffsetTable[-3],
    0,
    (RPC_SYNTAX_IDENTIFIER*)&_NDR64_RpcTransferSyntax_1_0,
    2,
    (MIDL_SYNTAX_INFO*)IDelayAPOSFX_SyntaxInfo
    };
CINTERFACE_PROXY_VTABLE(3) _IDelayAPOSFXProxyVtbl = 
{
    0,
    &IID_IDelayAPOSFX,
    IUnknown_QueryInterface_Proxy,
    IUnknown_AddRef_Proxy,
    IUnknown_Release_Proxy
};

const CInterfaceStubVtbl _IDelayAPOSFXStubVtbl =
{
    &IID_IDelayAPOSFX,
    &IDelayAPOSFX_ServerInfo,
    3,
    0, /* pure interpreted */
    CStdStubBuffer_METHODS
};

#ifdef __cplusplus
namespace {
#endif
static const MIDL_STUB_DESC Object_StubDesc = 
    {
    0,
    NdrOleAllocate,
    NdrOleFree,
    0,
    0,
    0,
    0,
    0,
    DelayAPOInterface__MIDL_TypeFormatString.Format,
    1, /* -error bounds_check flag */
    0x60001, /* Ndr library version */
    0,
    0x8010274, /* MIDL Version 8.1.628 */
    0,
    0,
    0,  /* notify & notify_flag routine table */
    0x2000001, /* MIDL flag */
    0, /* cs routines */
    0,   /* proxy/server info */
    0
    };
#ifdef __cplusplus
}
#endif

const CInterfaceProxyVtbl * const _DelayAPOInterface_ProxyVtblList[] = 
{
    ( CInterfaceProxyVtbl *) &_IDelayAPOSFXProxyVtbl,
    ( CInterfaceProxyVtbl *) &_IDelayAPOMFXProxyVtbl,
    0
};

const CInterfaceStubVtbl * const _DelayAPOInterface_StubVtblList[] = 
{
    ( CInterfaceStubVtbl *) &_IDelayAPOSFXStubVtbl,
    ( CInterfaceStubVtbl *) &_IDelayAPOMFXStubVtbl,
    0
};

PCInterfaceName const _DelayAPOInterface_InterfaceNamesList[] = 
{
    "IDelayAPOSFX",
    "IDelayAPOMFX",
    0
};


#define _DelayAPOInterface_CHECK_IID(n)	IID_GENERIC_CHECK_IID( _DelayAPOInterface, pIID, n)

int __stdcall _DelayAPOInterface_IID_Lookup( const IID * pIID, int * pIndex )
{
    IID_BS_LOOKUP_SETUP

    IID_BS_LOOKUP_INITIAL_TEST( _DelayAPOInterface, 2, 1 )
    IID_BS_LOOKUP_RETURN_RESULT( _DelayAPOInterface, 2, *pIndex )
    
}

EXTERN_C const ExtendedProxyFileInfo DelayAPOInterface_ProxyFileInfo = 
{
    (PCInterfaceProxyVtblList *) & _DelayAPOInterface_ProxyVtblList,
    (PCInterfaceStubVtblList *) & _DelayAPOInterface_StubVtblList,
    (const PCInterfaceName * ) & _DelayAPOInterface_InterfaceNamesList,
    0, /* no delegation */
    & _DelayAPOInterface_IID_Lookup, 
    2,
    2,
    0, /* table of [async_uuid] interfaces */
    0, /* Filler1 */
    0, /* Filler2 */
    0  /* Filler3 */
};
#if _MSC_VER >= 1200
#pragma warning(pop)
#endif


#endif /* defined(_M_ARM64) */

