#pragma once
#include <cstdint>

// Pragma pack garantiza que no haya padding entre campos, lo cual es crucial para leer/escribir estructuras directamente desde/hacia archivos binarios .mia
#pragma pack(push, 1)

// Structure Partition
struct Partition
{
    char Status;
    char Type;
    char Fit;
    int32_t Start;
    int32_t Size;
    char Name[16];
    int32_t Correlative; // Número de montaje (-1 hasta que sea montada)
    char Id[4];
};

// Structure MBR
struct MBR
{
    int32_t MbrSize;
    char CreationDate[19];
    int32_t Signature;
    char Fit;
    Partition Partitions[4];
};

// Structure EBR
struct EBR
{
    char Mount;
    char Fit;
    int32_t Start;
    int32_t Size;
    int32_t Next;
    char Name[16];
};

// ----------------------------------------------------
//  Estructuras para el sistema de archivos EXT2
// ----------------------------------------------------
// SuperBloque, metadatos del sistema de archivos EXT2
struct SuperBloque
{
    int32_t s_filesystem_type;   // Tipo de FS: 2 para EXT2
    int32_t s_inodes_count;      // Total de inodos
    int32_t s_blocks_count;      // Total de bloques
    int32_t s_free_blocks_count; // Bloques libres
    int32_t s_free_inodes_count; // Inodos libres
    int64_t s_mtime;             // Última fecha de montaje (epoch)
    int64_t s_umtime;            // Última fecha de desmontaje (epoch)
    int32_t s_mnt_count;         // Veces que ha sido montado
    int32_t s_magic;             // Valor mágico: 0xEF53
    int32_t s_inode_s;           // Tamaño del struct Inodo
    int32_t s_block_s;           // Tamaño de un bloque (64 bytes)
    int32_t s_firts_ino;         // Primer inodo libre (índice)
    int32_t s_first_blo;         // Primer bloque libre (índice)
    int32_t s_bm_inode_start;    // Byte de inicio del bitmap de inodos
    int32_t s_bm_block_start;    // Byte de inicio del bitmap de bloques
    int32_t s_inode_start;       // Byte de inicio de la tabla de inodos
    int32_t s_block_start;       // Byte de inicio de la tabla de bloques
};

// Inodo, índice de un archivo o carpeta
struct Inodo
{
    int32_t i_uid;       // UID del propietario
    int32_t i_gid;       // GID del grupo
    int32_t i_size;      // Tamaño en bytes del archivo
    int64_t i_atime;     // Último acceso (epoch)
    int64_t i_ctime;     // Creación (epoch)
    int64_t i_mtime;     // Última modificación (epoch)
    int32_t i_block[15]; // [0..11] directos, [12] ind. simple, [13] doble, [14] triple
    char i_type;         // '0' = carpeta, '1' = archivo
    char i_perm[3];      // Permisos UGO en octal, ej: "664"
};

// Contenido de una entrada en un bloque de carpeta
struct BContent
{
    char b_name[12]; // Nombre del archivo o carpeta (max 12 chars)
    int32_t b_inodo; // Índice del inodo correspondiente
};

// BloqueDIR para carpetas, contiene hasta 4 entradas de carpeta
struct BloqueDir
{
    BContent b_content[4]; // 4 entradas de carpeta (4 * 16 bytes = 64 bytes)
};

// BloqueFile para archivos, contiene 64 bytes de contenido
struct BloqueFile
{
    char b_content[64]; // 64 bytes de contenido de archivo
};

// BloqueApunt para bloques de apuntadores (indirectos), contiene 16 punteros a bloques
struct BloqueApunt
{
    int32_t b_pointers[16]; // 16 * 4 = 64 bytes; -1 si no usado
};

#pragma pack(pop) // Fin de pragma pack