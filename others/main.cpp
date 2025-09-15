#include <iostream>
#include <string>
#include "usn_reader.hh"

std::map<ULONGLONG, std::wstring> pendingRenames;


int wmain()
{
    std::wstring volumeLetter = L"C:";

    USNJournalReader reader(volumeLetter);
    return reader.Run();
}
