# SH-2 recompilation core

Current licensing and the grant for existing owner contributions are documented in [LICENSE-SCOPE.md](LICENSE-SCOPE.md).

A platform-neutral C11 core for static SH-2 recompilation. It implements
power-on vector reads and a first instruction subset covering integer and
control operations, selected memory transfers and delayed branches. Board
consumers supply the bus, firmware and timeline. Each processor has its own
state and callbacks.

The `sh2-aot` tool emits C operation functions from a big-endian image.
Strict execution checks the current fetch word at the guest PC before
calling a generated function. The separately linked interpreter shares the
instruction semantics and can continue after a catalog miss. Decode misses,
bus waits and faults are explicit results. Exception delivery, interrupt
sequencing and the remaining instruction semantics are further bring-up work.

The optional SH7604 bus controller models the keyed control registers,
refresh counter, compare flag and mode-register write window. Board bindings
advance it with SH-2 clock counts, consume refresh requests and apply memory
effects. The component uses the register layout from section 7 of the
[Hitachi SH7604 Hardware Manual](https://antime.kapsi.fi/sega/files/sh7604.pdf).

`Sh7604System` handles the standby and cache control registers. The caller
owns peripheral clock gating, cache storage, and the purge of valid entries
and replacement state. The register layout and reset values follow sections
8.2.1 and 14.2.1 of the [Renesas SH7604 Hardware Manual](https://www.renesas.com/ja/document/mah/superh-risc-engine-sh7604).

`Sh7604Cache` supplies write-through caching for ordinary memory spans. It
uses the processor's 16-byte lines, pseudo-LRU replacement and read-fill
controls. Consumers supply the live backing bytes and choose cacheable or
cache-through addressing.

## Build

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure --timeout 0
```

Consumers can use `add_subdirectory` and link `sh2-recomp-core::core` for
strict execution or `sh2-recomp-core::interpreter` for fallback.
Run `sh2-aot input.bin catalog.c name [base]` with the host-built generator.
The caller provides the guest addresses and links the emitted
`sh2_catalog_name` from the generated source. Keep generated catalogs
derived from owned guest inputs in the consumer's private build directory.

The fixtures exercise power-on reset, delayed call and return, a retried
store, signed loads, native and interpreter parity, and fallback on a
changed fetch word. Instruction encodings and operands follow the
[Renesas SH-1/SH-2/SH-DSP Software Manual](https://www.renesas.com/en/document/mas/sh-1sh-2sh-dsp-software-manual?r=1054686).
See [LICENSE](LICENSE) for the project license.
