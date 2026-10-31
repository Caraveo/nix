; Intent emulation kernel.
; The resident assembler in NIX builds this source.
; NIX stays the host. This guest only reports that it came up.
org 0x8000

entry:
  mov esi, message
  mov edi, 0x1000
  mov ecx, message_end - message
  rep movsb
  xor eax, eax
  vmmcall
  hlt

message:
  db "intent kernel online. NIX manages this guest.", 0
message_end:
