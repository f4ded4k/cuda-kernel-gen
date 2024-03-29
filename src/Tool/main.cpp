#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>

#include <clang/Tooling/CommonOptionsParser.h>
#include <llvm/Support/CommandLine.h>

#include "ASTContext.h"
#include "FileHelper.h"
#include "KernelManager.h"
#include "KernelWriter.h"
#include "SourceParser.h"
#include "TemplateParser.h"

using namespace ckg;
namespace cl = llvm::cl;
namespace tooling = clang::tooling;

static cl::OptionCategory MyToolCatagory("CudaKernelGen options");
static cl::opt<std::string>
    CkgFolderStr("ckg-path", cl::Required,
                 cl::desc("Specify top-level ckg folder"),
                 cl::value_desc("ckg-folder-path"), cl::cat(MyToolCatagory));
static cl::opt<bool>
    CompileOnlyFlag("c", cl::init(false),
                    cl::desc("Stop after compilation without linking"),
                    cl::cat(MyToolCatagory));
static cl::opt<bool> QuietFlag("q", cl::init(false),
                               cl::desc("Hide progress messages"),
                               cl::cat(MyToolCatagory));
static cl::opt<bool> RevertFlag("revert", cl::init(false),
                                cl::desc("Revert changes made to EtExpr.h"),
                                cl::cat(MyToolCatagory));

std::vector<std::string> parseSourceFiles(tooling::CommonOptionsParser &OP) {
  if (!QuietFlag)
    std::cout << "Parsing sources..." << std::endl;
  std::vector<std::string> templateList;
  SourceParser::parseSources(OP.getSourcePathList(), OP.getCompilations(),
                             templateList);
  return templateList;
}

void removeSpaces(std::string &S) {
  int32_t J = 0;
  for (int32_t I = 0; I < S.size(); ++I) {
    if (S[I] != ' ') {
      S[J++] = S[I];
    }
  }
  S.erase(J);
}

KernelManager generateKernels(std::vector<std::string> &templateList) {
  if (!QuietFlag)
    std::cout << "Generating kernels..." << std::endl;
  KernelManager KM;
  for (auto &&S : templateList) {
    removeSpaces(S);
    TemplateParser TP(S);
    ASTContext C = TP.createAST();
    KM.createNewKernel(C, S);
  }
  return KM;
}

void writeKernels(KernelManager &KM, const fs::path &CkgFolder) {
  if (!QuietFlag)
    std::cout << "Writing kernels..." << std::endl;
  KernelWriter KW(CkgFolder, KM);
  KW.writeKernelCalls();
  KW.writeKernelsHeader();
  KW.writeKernelsSource();
  KW.writeKernelWrappersHeader();
  KW.writeKernelWrappersSource();
}

void compileKernels(const fs::path &ckgFolder) {
  if (!QuietFlag)
    std::cout << "Compiling kernels..." << std::endl;
  std::string NvccCommand =
      "nvcc -c -O2 -odir=" + (ckgFolder / "obj").string() + " " +
      (ckgFolder / "include" / "MyKernels.cu").string() + " " +
      (ckgFolder / "include" / "MyKernelWrappers.cu").string();
  system(NvccCommand.c_str());
}

void compileAndLink(const fs::path &ckgFolder,
                    const std::vector<std::string> &sourceFiles) {
  if (!QuietFlag)
    std::cout << "Linking everything..." << std::endl;
  std::string ClangCommand = "clang++ -O2 -std=c++17 ";
  fs::path CudaPath = std::getenv("CUDA_PATH");
  ClangCommand += "-I\"" + (CudaPath / "include").string() + "\" ";
  ClangCommand += "-I\"" + (ckgFolder / "include").string() + "\" ";
  ClangCommand +=
      "-l\"" + (CudaPath / "lib" / "x64" / "cudart.lib").string() + "\" ";
  for (auto &&source : sourceFiles) {
    ClangCommand += source + " ";
  }
  ClangCommand += (ckgFolder / "obj" / "MyKernels.obj").string() + " ";
  ClangCommand += (ckgFolder / "obj" / "MyKernelWrappers.obj").string() + " ";
  system(ClangCommand.c_str());
}

    int main(int argc, const char **argv) {
  tooling::CommonOptionsParser OP(argc, argv, MyToolCatagory);
  fs::path CkgFolder = fs::canonical(CkgFolderStr.getValue());

  if (RevertFlag && CompileOnlyFlag) {
    std::cout << "Warning: EtExpr.h is reverted without linking it against the "
                 "sources."
              << std::endl;
  }

  FileHelper::initiateCkgFolder(CkgFolder);
  auto templateList = parseSourceFiles(OP);
  auto KM = generateKernels(templateList);
  writeKernels(KM, CkgFolder);
  compileKernels(CkgFolder);
  if (!CompileOnlyFlag) {
    compileAndLink(CkgFolder, OP.getSourcePathList());
  }
  if (RevertFlag) {
    FileHelper::revertOriginals(CkgFolder);
  }
  FileHelper::deleteTempFolder(CkgFolder);

  return 0;
}