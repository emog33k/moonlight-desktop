#include "filepicker_win32.h"

#include <shobjidl.h>

#include <string>
#include <thread>

namespace LocalRdp {

void pickFilesAsync(HWND owner, const QString& title, std::function<void(QStringList)> done)
{
    const std::wstring wtitle = title.toStdWString();
    std::thread([owner, wtitle, done]() {
        QStringList paths;
        if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
            IFileOpenDialog* dialog = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                           IID_PPV_ARGS(&dialog)))) {
                DWORD options = 0;
                dialog->GetOptions(&options);
                dialog->SetOptions(options | FOS_ALLOWMULTISELECT | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
                dialog->SetTitle(wtitle.c_str());
                if (SUCCEEDED(dialog->Show(owner))) {
                    IShellItemArray* items = nullptr;
                    if (SUCCEEDED(dialog->GetResults(&items))) {
                        DWORD count = 0;
                        items->GetCount(&count);
                        for (DWORD i = 0; i < count; i++) {
                            IShellItem* item = nullptr;
                            if (SUCCEEDED(items->GetItemAt(i, &item))) {
                                PWSTR path = nullptr;
                                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                                    paths << QString::fromWCharArray(path);
                                    CoTaskMemFree(path);
                                }
                                item->Release();
                            }
                        }
                        items->Release();
                    }
                }
                dialog->Release();
            }
            CoUninitialize();
        }
        done(paths);
    }).detach();
}

}
