# Levin Search / Kolmogorov Complexity / Metabiology

Implementación en C de búsqueda universal de Levin, cotas de complejidad de
Kolmogorov y metabiología (evolución de Chaitin) sobre una máquina virtual
de pila de 16 opcodes.

## Máquina virtual

Programas: secuencias de bytes. 16 opcodes (`HALT`, `OUT`, `DUP`, `SWAP`,
`ADD`, `SUB`, `MUL`, `INC`, `DEC`, `PUSH0/1/2`, `LIT`, `JMP`, `JZ`, `JNZ`).
`LIT`/`JMP`/`JZ`/`JNZ` consumen un byte de argumento. Aritmética acotada a
±`INT_CAP` para evitar desbordes.
