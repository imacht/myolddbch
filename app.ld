MEMORY {
  flash (rx) : ORIGIN = 0x00000000, LENGTH = 0x00100000
  ram  (rwx) : ORIGIN = 0x20000000, LENGTH = 0x00040000
}

FLASH_PAGE_SIZE = 2048;
SIMEEPROM_SIZE = 8192;
PSSTORE_SIZE = 16384;

/* Create useful defines for the gcc.ld file to use */
RAM_BOTTOM          = 0x20000000;
RAM_TOP             = 0x2003FFFF;

MFB_BOTTOM          = 0x00000000;
MFB_TOP             = 0x000FFFFF;

/* If we were given a RAM_RETAIN_SIZE then make sure that we don't use */
/* any RAM outside of that range by setting it to be in the unused region. */
UNUSED_RAM_SIZE = DEFINED(RAM_RETAIN_SIZE) ? (RAM_TOP - RAM_BOTTOM + 1 - RAM_RETAIN_SIZE): 0;

/* Map out all of the regions we use in our applications */
SECTIONS {

  .vectors MFB_BOTTOM :
  {
    PROVIDE(__VECTORS__begin = .);
    KEEP( *(.vectors .vectors.*) );
    . = ALIGN(2);  /* Align each segment to a Flash word boundary */
    PROVIDE(__VECTORS__end = .);
  }>flash
  PROVIDE(__VECTORS__size = SIZEOF(.vectors));

  .textrw_init . :
  {
    PROVIDE(__TEXTRW_INIT__begin = .);
    PROVIDE(__ram_func_section_start = .);
    * (.textrw_init .textrw_init.*);
    . = ALIGN(2);  /* Align each segment to a Flash word boundary */
    PROVIDE(__TEXTRW_INIT__end = .);
    PROVIDE(__ram_func_section_end = .);
  }>flash
  PROVIDE(__TEXTRW_INIT__size = SIZEOF(.textrw_init));

  .text . :
  {
    KEEP(*(.init))
    KEEP(*(.fini))

    PROVIDE(__TEXT__begin = .);
    * (.text .text.*);
    . = ALIGN(2);  /* Align each segment to a Flash word boundary */
    PROVIDE(__TEXT__end = .);
  }>flash
  PROVIDE(__TEXT__size = SIZEOF(.text));

  .rodata . :
  {
    PROVIDE(__CONST__begin = .);
    * (.rodata .rodata.*);
    . = ALIGN(2);  /* Align each segment to a Flash word boundary */
    PROVIDE(__CONST__end = .);
  }>flash
  PROVIDE(__CONST__size = SIZEOF(.rodata));

  .data_init . :
  {
    PROVIDE(__DATA_INIT__begin = .);
    PROVIDE(__etext = .);

    /* No need to place anything here explicitly */
    /* Just reserve room for .data to relocate here */

    PROVIDE(__DATA_INIT__end = __DATA_INIT__begin + SIZEOF(.data));
  }>flash
  PROVIDE(__DATA_INIT__size = SIZEOF(.data));

  ASSERT ( . < MFB_TOP - SIMEEPROM_SIZE - PSSTORE_SIZE + 1 , "

======================================================================
======== LINKER ERROR: Not enough flash (Application space)
======================================================================
" )

  . = MFB_TOP - SIMEEPROM_SIZE - PSSTORE_SIZE + 1;
  .simee ALIGN(FLASH_PAGE_SIZE) (NOLOAD) :
  {
    KEEP( * (.simee .simee.*) );
  }>flash
  OUR_BEGIN = (SIZEOF(.simee) == 0) ? 0 : ADDR(.simee);
  PROVIDE(__SIMEE__begin = OUR_BEGIN);
  PROVIDE(__SIMEE__end = OUR_BEGIN + SIZEOF(.simee));
  PROVIDE(__SIMEE__size = SIZEOF(.simee));

  ASSERT ( __SIMEE__size == SIMEEPROM_SIZE , "

======================================================================
======== LINKER ERROR: SIMEEPROM_SIZE is misconfigured
======================================================================
" )

  . = MFB_TOP - PSSTORE_SIZE + 1;
  .internal_storage . (NOLOAD) :
  {
    * (.internal_storage .internal_storage.*);
  }>flash
  OUR_BEGIN = (SIZEOF(.internal_storage) == 0) ? 0 : ADDR(.internal_storage);
  PROVIDE(__INTERNAL_STORAGE__begin = OUR_BEGIN);
  PROVIDE(__INTERNAL_STORAGE__end = OUR_BEGIN + SIZEOF(.internal_storage));
  PROVIDE(__INTERNAL_STORAGE__size = SIZEOF(.internal_storage));

  . = MFB_TOP - PSSTORE_SIZE + 1;
  .psstore ALIGN(FLASH_PAGE_SIZE) (NOLOAD) :
  {
    KEEP( * (.psstore .psstore.*) );
  }>flash
  OUR_BEGIN = (SIZEOF(.psstore) == 0) ? 0 : ADDR(.psstore);
  PROVIDE(__PSSTORE__begin = OUR_BEGIN);
  PROVIDE(__PSSTORE__end = OUR_BEGIN + SIZEOF(.psstore));
  PROVIDE(__PSSTORE__size = SIZEOF(.psstore));

  ASSERT ( __PSSTORE__size == PSSTORE_SIZE , "

======================================================================
======== LINKER ERROR: PSSTORE_SIZE is misconfigured
======================================================================
" )

  /* Sanity check */
  ASSERT (. <= MFB_TOP + 1 , "

======================================================================
======== LINKER ERROR: Not enough flash
======================================================================
" )

  /* --------------------------------------------------------------------*/

  . = RAM_BOTTOM;

  .resetinfo RAM_BOTTOM (NOLOAD) :
  {
    PROVIDE(__RESETINFO__begin = .);
    * (.resetinfo .resetinfo.*);
    PROVIDE(__RESETINFO__end = .);
  }>ram
  PROVIDE(__RESETINFO__size = SIZEOF(.resetinfo));

  .textrw (NOLOAD) :
  {
    PROVIDE(__TEXTRW__begin = .);
    * (.textrw .textrw.*);
    PROVIDE(__TEXTRW__end = .);
  }>ram
  PROVIDE(__TEXTRW__size = SIZEOF(.textrw));

  .data . : AT(__DATA_INIT__begin)
  {
    PROVIDE(__data_start__ = .);
    . = ALIGN(4);
    /* preinit data */
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP(*(.preinit_array))
    PROVIDE_HIDDEN (__preinit_array_end = .);
    . = ALIGN(4);
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array))
    PROVIDE_HIDDEN (__init_array_end = .);
    . = ALIGN(4);
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array))
    PROVIDE_HIDDEN (__fini_array_end = .);

    PROVIDE(__DATA__begin = .);
    * (.data .data.*);
    . = ALIGN(2);  /* Align each segment to a Flash word boundary */
    PROVIDE(__data_end__ = .);
    PROVIDE(__DATA__end = .);
  }>ram
  PROVIDE(__DATA__size = SIZEOF(.data));

  .memp . (NOLOAD) :
  {
    PROVIDE(_smemp = .);
    KEEP( * (.bss.memp*) );
    KEEP( *memp.o(.bss) );
  }>ram
  PROVIDE(_memp_size = SIZEOF(.memp));

  .bss . (NOLOAD) :
  {
    PROVIDE(__BSS__begin = .);
    PROVIDE(__bss_start__ = .);
    * (.bss .bss.*);
    * (COMMON);
    * (.platform .platform.*);
    PROVIDE(__bss_end__ = .);
    PROVIDE(__BSS__end = .);
  }>ram
  PROVIDE(__BSS__size = SIZEOF(.bss));

  /* Legacy __NO_INIT__ section. See comment above .noinitnew. */
  .noinitlegacy . (NOLOAD) :
  {
    * (.noinitlegacy .noinitlegacy.*);
  }>ram
  OUR_BEGIN = (SIZEOF(.noinitlegacy) == 0) ? 0 : ADDR(.noinitlegacy);
  PROVIDE(__NO_INIT_LEGACY__begin = OUR_BEGIN);
  PROVIDE(__NO_INIT_LEGACY__end = OUR_BEGIN + SIZEOF(.noinitlegacy));
  PROVIDE(__NO_INIT_LEGACY__size = SIZEOF(.noinitlegacy));

  .debugChannel . (NOLOAD) :
  {
    * (DEBUG_CHANNEL DEBUG_CHANNEL.*)
  }>ram
  OUR_BEGIN = (SIZEOF(.debugChannel) == 0) ? 0 : ADDR(.debugChannel);
  PROVIDE(__DEBUG_CHANNEL__begin = OUR_BEGIN);
  PROVIDE(__DEBUG_CHANNEL__end = OUR_BEGIN + SIZEOF(.debugChannel));
  PROVIDE(__DEBUG_CHANNEL__size = SIZEOF(.debugChannel));

  .emheap ALIGN(8) (NOLOAD) :
  {
    PROVIDE(__EMHEAP__begin = .);
    KEEP (* (.emheap .emheap.*) );
    PROVIDE(__EMHEAP__end = .);
  }>ram
  PROVIDE(__EMHEAP__size = SIZEOF(.emheap));

  .guard_region ALIGN(0x20) (NOLOAD) :
  {
    PROVIDE(__GUARD_REGION__begin = .);
    KEEP ( *(.guard_region .guard_region.*) );
    PROVIDE(__GUARD_REGION__end = .);
  }>ram
  PROVIDE(__GUARD_REGION__size = SIZEOF(.guard_region));

  _left = RAM_TOP - __GUARD_REGION__end + 1 - UNUSED_RAM_SIZE;
  .cstack ALIGN(8) (NOLOAD):
  {
    PROVIDE(__CSTACK__begin = .);
/*    KEEP( *(.cstack .cstack.*) );*/
    . = . + _left;
    PROVIDE(__StackTop = .);
    PROVIDE(__stack = __StackTop);
    PROVIDE(__CSTACK__end = .);
    __StackLimit = __CSTACK__end;
  }>ram
  PROVIDE(__CSTACK__size = SIZEOF(.cstack));

  RAM_GUARD = RAM_TOP - UNUSED_RAM_SIZE ;

  ASSERT ( . <= RAM_GUARD + 1, "

======================================================================
======== LINKER ERROR: Not enough RAM
======================================================================
" )

  . = RAM_TOP - UNUSED_RAM_SIZE;
  UNRETAINED_RAM (NOLOAD) :
  {
    PROVIDE(__UNRETAINED_RAM__begin = .);
    * (UNRETAINED_RAM UNRETAINED_RAM.*)
    PROVIDE(__UNRETAINED_RAM__end = .);
  }>ram
  PROVIDE(__UNRETAINED_RAM__size = SIZEOF(UNRETAINED_RAM));

  ASSERT ( . < RAM_TOP + 1 , "

======================================================================
======== LINKER ERROR: Not enough RAM (Unretained)
======================================================================
" )

  __RAM__end = .;
  PROVIDE(end = .);
  PROVIDE(_end = .);
}
