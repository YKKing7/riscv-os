/*
 * RISC-V 内核链接脚本
 * 控制程序内存布局，定义各段位置和符号
 */

OUTPUT_ARCH(riscv)
ENTRY(_start)

PHDRS {
  text PT_LOAD FLAGS(5);   /* 代码段: r-x */
  data PT_LOAD FLAGS(6);   /* 数据段: rw- */
}

SECTIONS {
  . = 0x80000000;

  /* 代码段 */
  .text : {
    _stext = .;
    *(.text.entry)
    *(.text .text.*)
    _etext = .;
  } :text

  /* 只读数据段 */
  .rodata : {
    . = ALIGN(4096);
    _srodata = .;
    *(.rodata .rodata.*)
    _erodata = .;
  } :text

  /* 初始化数据段 */
  .data : {
    . = ALIGN(4096);
    _sdata = .;
    *(.data .data.*)
    _edata = .;
  } :data

  /* 未初始化数据段 */
  .bss : {
    . = ALIGN(4096);
    _sbss = .;
    *(.bss .bss.*)
    *(.sbss .sbss.*)
    *(COMMON)
    _ebss = .;
  } :data

  . = ALIGN(4096);
  _end = .;
}
