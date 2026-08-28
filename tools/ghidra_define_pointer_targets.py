# Define a function at every code address this binary only ever reaches through a POINTER.
#
# WHY. Auto-analysis on Cyberpunk2077.exe leaves whole classes of function undefined, and it is not a
# failure of the analyser -- nothing calls them. Three kinds turned up in one afternoon of this
# project alone:
#
#   * NATIVE SCRIPT HANDLERS. Registered as `FUN_1403cdf14(&storage, class, name, name, HANDLER, 0)`,
#     so the only reference to the handler's first byte is a data reference from a LEA.
#   * JOB BODIES. `Tick_UsersLookAt` builds a job whose body is LAB_140cc6b2c and hands the pointer
#     to the scheduler; the body is never called from anywhere the analyser can see.
#   * VIRTUAL METHODS reached only through a vtable in .rdata.
#
# The decompiler works on Function objects, so every one of these answers "no function found" however
# long the analysis ran, and the only cure has been pressing F on one address at a time. That does not
# scale: this program has thousands of them, and each is a question the project may have to ask.
#
# WHAT IT DOES. Walks every address in executable memory that something REFERENCES, keeps the ones
# that are referenced without control flow (a pointer, not a call or a jump), and where no function
# exists yet, disassembles and creates one.
#
# WHY IT IS SAFE TO RUN. An address already INSIDE a function body is skipped -- that is the case that
# would do damage, splitting a real function in two because a switch table or a loop label happened to
# be referenced. Only addresses no function claims are touched, and an address that will not
# disassemble is left alone and counted. Running it twice is a no-op.
#
# It is slow (it walks the whole reference table) and it will define many thousands of functions.
# Ghidra stays responsive; the console prints progress. Cancel from the script's progress dialog at any
# time -- what has been created stays created.
#
# HOW TO RUN. Ghidra -> Window -> Script Manager -> Manage Script Directories -> add this folder, then
# run this script with Cyberpunk2077.exe open. Consider saving the project first, so the whole pass can
# be undone by reverting rather than by hand.
#
# @category CyberpunkVRPort

from ghidra.program.model.address import AddressSet

listing = currentProgram.getListing()
memory = currentProgram.getMemory()
fm = currentProgram.getFunctionManager()
refs = currentProgram.getReferenceManager()

# Executable memory, block by block, so the reference walk never leaves .text.
code = AddressSet()
for block in memory.getBlocks():
    if block.isExecute():
        code.addRange(block.getStart(), block.getEnd())

print("executable memory: %s" % code)

made, skipped_inside, skipped_have, failed, looked = 0, 0, 0, 0, 0

dests = refs.getReferenceDestinationIterator(code, True)
while dests.hasNext():
    if monitor.isCancelled():
        print("cancelled")
        break

    addr = dests.next()
    looked += 1
    if looked % 20000 == 0:
        print("  ... %d addresses looked at, %d functions created" % (looked, made))

    if getFunctionAt(addr) is not None:
        skipped_have += 1
        continue
    # THE GUARD THAT MATTERS. Inside an existing function this address is a label, not an entry point,
    # and defining a function on it would cut a working function in half.
    if fm.getFunctionContaining(addr) is not None:
        skipped_inside += 1
        continue

    # Referenced by a pointer, not by control flow. A call or jump target the analyser did not turn
    # into a function usually has a reason (a thunk, a tail call into the middle of something), and
    # this script deliberately does not argue with it.
    pointed_at = False
    for ref in refs.getReferencesTo(addr):
        if not ref.getReferenceType().isFlow():
            pointed_at = True
            break
    if not pointed_at:
        continue

    if getInstructionAt(addr) is None:
        disassemble(addr)
        if getInstructionAt(addr) is None:
            failed += 1
            continue

    if createFunction(addr, None) is None:
        failed += 1
    else:
        made += 1

print("done: %d functions created, %d already defined, %d inside existing functions, %d would not disassemble"
      % (made, skipped_have, skipped_inside, failed))
