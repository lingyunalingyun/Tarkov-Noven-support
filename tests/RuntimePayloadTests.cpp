#include "common/AppPaths.h"
#include "ocr/TextDetector.h"
#include "ocr/TextRecognizer.h"
#include "data/ItemCatalog.h"
#include <iostream>
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;std::filesystem::path program{argv[1]};
    noven::ocr::TextDetector detector;noven::ocr::TextRecognizer recognizer;noven::data::ItemCatalog catalog;std::wstring error;
    if(!detector.Initialize(program/L"onnxruntime.dll",program/L"assets/models/ppocrv5_mobile_det.onnx",error)||!detector.WarmUp(error)
        ||!recognizer.Initialize(program/L"onnxruntime.dll",program/L"assets/models/ppocrv5_mobile_rec.onnx",program/L"assets/models/ppocrv5_mobile_rec_dict.txt",error)||!recognizer.WarmUp(error)
        ||!catalog.Load(program/L"assets/data/items_catalog.tsv",error)){std::wcerr<<error;return 1;}
    std::cout<<"real ONNX detector/recognizer warmup and catalog runtime payload PASS\n";return 0;
}
