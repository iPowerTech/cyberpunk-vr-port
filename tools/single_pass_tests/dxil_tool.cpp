#include <windows.h>
#include <unknwn.h>
#include <oleauto.h>
#include <dxcapi.h>
#include <wrl/client.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
static void Check(HRESULT hr,const char* operation) {
    if(FAILED(hr)){std::fprintf(stderr,"%s: %08x\n",operation,unsigned(hr));throw std::runtime_error(operation);}
}
static ComPtr<IDxcBlob> Result(IDxcOperationResult* result) {
    HRESULT hr{};Check(result->GetStatus(&hr),"GetStatus");
    ComPtr<IDxcBlobEncoding> errors;result->GetErrorBuffer(&errors);
    if(errors && errors->GetBufferSize())std::fwrite(errors->GetBufferPointer(),1,errors->GetBufferSize(),stderr);
    Check(hr,"DXC operation");ComPtr<IDxcBlob> blob;Check(result->GetResult(&blob),"GetResult");return blob;
}
int wmain(int argc,wchar_t** argv) try {
    if(argc<3)throw std::runtime_error("dxil_tool <DXC directory> --passes | <input.ll> <output.dxil> [optimizer passes...]");
    auto path=std::filesystem::path(argv[1])/L"dxcompiler.dll";
    HMODULE module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!module)throw std::runtime_error("LoadLibrary dxcompiler");
    auto create=reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(module,"DxcCreateInstance"));
    if(!create)throw std::runtime_error("DxcCreateInstance unavailable");
    ComPtr<IDxcOptimizer> optimizer;Check(create(CLSID_DxcOptimizer,IID_PPV_ARGS(&optimizer)),"Optimizer");
    if(std::wstring(argv[2])==L"--passes") {
        UINT32 count{};Check(optimizer->GetAvailablePassCount(&count),"Pass count");
        for(UINT32 i=0;i<count;++i) {
            ComPtr<IDxcOptimizerPass> pass;Check(optimizer->GetAvailablePass(i,&pass),"Pass");
            wchar_t* name=nullptr;wchar_t* description=nullptr;
            pass->GetOptionName(&name);pass->GetDescription(&description);
            std::wprintf(L"%s: %s\n",name,description);CoTaskMemFree(name);CoTaskMemFree(description);
        }
        return 0;
    }
    if(argc<4)throw std::runtime_error("output path required");
    ComPtr<IDxcUtils> utils;Check(create(CLSID_DxcUtils,IID_PPV_ARGS(&utils)),"Utils");
    ComPtr<IDxcBlobEncoding> input;Check(utils->LoadFile(argv[2],nullptr,&input),"LoadFile");
    ComPtr<IDxcBlob> assembly=input;
    if(argc>4) {
        std::vector<LPCWSTR> passes;for(int i=4;i<argc;++i)passes.push_back(argv[i]);
        ComPtr<IDxcBlobEncoding> messages;ComPtr<IDxcBlob> optimized;
        const HRESULT hr=optimizer->RunOptimizer(input.Get(),passes.data(),UINT32(passes.size()),&optimized,&messages);
        if(messages && messages->GetBufferSize())std::fwrite(messages->GetBufferPointer(),1,messages->GetBufferSize(),stderr);
        Check(hr,"RunOptimizer");assembly=optimized;
    }
    ComPtr<IDxcAssembler> assembler;Check(create(CLSID_DxcAssembler,IID_PPV_ARGS(&assembler)),"Assembler");
    ComPtr<IDxcOperationResult> result;Check(assembler->AssembleToContainer(assembly.Get(),&result),"AssembleToContainer");
    auto blob=Result(result.Get());
    ComPtr<IDxcValidator> validator;Check(create(CLSID_DxcValidator,IID_PPV_ARGS(&validator)),"Validator");
    result.Reset();Check(validator->Validate(blob.Get(),DxcValidatorFlags_InPlaceEdit,&result),"Validate");
    blob=Result(result.Get());
    std::ofstream out(std::filesystem::path(argv[3]),std::ios::binary);
    out.write(static_cast<const char*>(blob->GetBufferPointer()),blob->GetBufferSize());
    if(!out)throw std::runtime_error("writing output failed");
    std::printf("PASS assembled and validated %zu bytes\n",blob->GetBufferSize());return 0;
} catch(const std::exception& e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
