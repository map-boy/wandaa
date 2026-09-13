#include "../include/pe_writer.hpp"
int main(){
    PEWriter pe;
    int exitProcess = pe.addImport("kernel32.dll", "ExitProcess");
    size_t entry = pe.beginCode();
    pe.setEntryOffset(entry);
    pe.emit({0xB9, 0x2A, 0x00, 0x00, 0x00}); // mov ecx, 42
    pe.emit({0x48, 0x83, 0xEC, 0x28});       // sub rsp, 40 (shadow space)
    pe.callImport(exitProcess);              // call [rip+disp] -> ExitProcess(42)
    pe.emit({0xC3});                         // ret (safety net)
    pe.writeExe("selftest.exe");
    return 0;
}