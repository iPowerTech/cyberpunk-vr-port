# Create (and name) a function at every native script handler in Cyberpunk2077.exe.
#
# WHY THIS EXISTS. Auto-analysis leaves the native handlers undefined, and it is not a bug in the
# analyser: nothing CALLS them. Each one is registered as a pointer,
#
#     FUN_1403cdf14(&storage, class, name, shortName, HANDLER, 0)
#
# so the only reference to the handler's first byte is a data reference from a LEA. Ghidra's function
# analyser follows calls, so the handler stays a LAB_ with instructions under it and no Function
# object -- and the decompiler works on Function objects, so `decompile 0x14249ba44` answers "no
# function found" no matter how long the analysis ran. Pressing F on each address by hand works and
# does not scale: there are thousands of natives, and every one of them is a question this project
# might have to ask.
#
# WHAT IT DOES. For each call to the registrar it takes the data references out of the calling
# function, keeps the one that lands in executable memory (that is the handler), disassembles it if
# needed, creates a function there, and names it from the CName string the same caller passes -- so
# `IsInteractionLookAtTarget` stops being LAB_14249ba44 and becomes nat_IsInteractionLookAtTarget.
#
# HOW TO RUN IT. Ghidra -> Window -> Script Manager -> Manage Script Directories -> add this folder,
# then run this script with the Cyberpunk2077.exe program open. Progress goes to the console. It is
# safe to run twice: an address that already has a function is skipped.
#
# THE ONE THING TO CHECK FIRST. REGISTRAR is the address of the registrar in YOUR database. It is the
# function called from the tiny per-native registration stubs; find one by searching for a known
# native's name string (say "GetLookAtObject"), following its single xref, and reading the call. It
# moves with the game version, and a wrong address here simply finds nothing rather than doing damage.
#
# @category CyberpunkVRPort

REGISTRAR = 0x1403cdf14

import re

from ghidra.program.model.symbol import SourceType

listing = currentProgram.getListing()
memory = currentProgram.getMemory()
fm = currentProgram.getFunctionManager()

NAME_OK = re.compile(r"^[A-Za-z_][A-Za-z0-9_]{2,}$")


def is_executable(addr):
    block = memory.getBlock(addr)
    return block is not None and block.isExecute()


def data_refs_out_of(func):
    """Every non-call reference the function makes, in address order."""
    out = []
    for instr in listing.getInstructions(func.getBody(), True):
        for ref in instr.getReferencesFrom():
            if ref.getReferenceType().isCall() or ref.getReferenceType().isJump():
                continue
            out.append(ref.getToAddress())
    return out


def name_from(func):
    """The CName the stub passes to the registrar, used to name the handler."""
    for addr in data_refs_out_of(func):
        if is_executable(addr):
            continue
        data = listing.getDataAt(addr)
        if data is None or not data.hasStringValue():
            continue
        text = str(data.getValue())
        if NAME_OK.match(text):
            return text
    return None


registrar = getFunctionAt(toAddr(REGISTRAR))
if registrar is None:
    print("No function at REGISTRAR = 0x%x -- fix the address at the top of this script." % REGISTRAR)
else:
    made, named, skipped, failed = 0, 0, 0, 0
    seen = set()

    for ref in getReferencesTo(registrar.getEntryPoint()):
        if not ref.getReferenceType().isCall():
            continue
        stub = fm.getFunctionContaining(ref.getFromAddress())
        if stub is None:
            continue

        label = name_from(stub)

        for target in data_refs_out_of(stub):
            if not is_executable(target):
                continue
            key = target.getOffset()
            if key in seen:
                continue
            seen.add(key)

            if getFunctionAt(target) is not None:
                skipped += 1
                continue

            # A handler with no instructions yet is the normal case: the bytes were never reached by
            # the analyser at all. Disassembling first is what makes the function creation succeed.
            if getInstructionAt(target) is None:
                disassemble(target)

            func = createFunction(target, None)
            if func is None:
                failed += 1
                continue
            made += 1

            if label:
                try:
                    func.setName("nat_" + label, SourceType.USER_DEFINED)
                    named += 1
                except Exception:
                    pass

    print("native handlers: %d created (%d named), %d already defined, %d could not be created"
          % (made, named, skipped, failed))
