.section .payload, "a"
.global payload_header
payload_header:
  .quad kernel_main
  .quad __bss_start
  .quad __bss_end
  .quad stack_top

.section .bss
.align 16
.skip 65536
.global stack_top
stack_top:
.skip 16384
.global aux_stack_top
aux_stack_top:
.skip 16384
.global vmx_stack_top
vmx_stack_top:
