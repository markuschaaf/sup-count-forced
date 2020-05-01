#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <error.h>
#include <sysexits.h>

struct In_file
{
    char const * const name;
    FILE * const handle;
    In_file( char const *name );
    In_file( char const *name, FILE *handle ) : name( name ), handle( handle ) {}
    ~In_file() { fclose( handle ); }
    In_file( In_file const & ) = delete;
    int get_char();
    uint_fast8_t read_u1();
    uint_fast16_t read_u2();
    uint_fast32_t read_u4();
    void expect( char const *bytes );
    off_t pos() const;
    void pos( off_t o );
    bool more();
};

In_file::In_file( char const *name )
:   name    ( name ),
    handle  ( fopen( name, "rb" ))
{
    if( !handle ) error( EX_NOINPUT, errno, "%s", name );
}

int In_file::get_char()
{
    int c = getc( handle );
    if( c == EOF && ferror( handle )) error( EX_IOERR, errno, "%s", name );
    return c;
}

uint_fast8_t In_file::read_u1()
{
    int c = get_char();
    if( c == EOF ) error( EX_DATAERR, 0, "%s: unexpected end of file", name );
    return c;
}

uint_fast16_t In_file::read_u2()
{
    uint_fast16_t v = read_u1();
    v = v << 8 | read_u1();
    return v;
}

uint_fast32_t In_file::read_u4()
{
    uint_fast32_t v = read_u2();
    v = v << 16 | read_u2();
    return v;
}

void In_file::expect( char const *bytes )
{
    for( char const *b = bytes; *b; ++b ) {
        uint_fast8_t c = read_u1();
        if( *b != c ) error( EX_DATAERR, 0,
            "%s: expected %#02x, got %#02x @offset %ju",
            name, (unsigned) *b, (unsigned) c, (uintmax_t) pos());
    }
}

off_t In_file::pos() const
{
    off_t o = ftello( handle );
    if( o == -1 ) error( EX_IOERR, errno, "%s: ftello", name );
    return o;
}

void In_file::pos( off_t o )
{
    if( fseeko( handle, o, SEEK_SET ) == -1 )
        error( EX_IOERR, errno, "%s: fseeko", name );
}

bool In_file::more()
{
    int c = get_char();
    if( c == EOF ) return false;
    ungetc( c, handle );
    return true;
}

enum class Time : uint_fast32_t;
enum class Obj_ID : uint_fast16_t;
enum class Comp_ID : uint_fast16_t;
enum class Window_ID : uint_fast8_t;
enum class Palette_ID : uint_fast8_t;

struct Segment
{
    typedef uint_fast16_t Size;
    Time time;
    Size size;
    enum class Type : uint_fast8_t { null, PDS = 0x14, ODS, PCS, WDS, END = 0x80 } type;
    Segment( In_file &f );
};

Segment::Segment( In_file &f )
{
    f.expect( "PG" );
    time = (Time)   f.read_u4();
    (void)          f.read_u4();
    type = (Type)   f.read_u1();
    size = (Size)   f.read_u2();
}

struct PCS
{
    uint_fast16_t width, height;
    Comp_ID comp_id;
    enum class State : uint_fast8_t { normal, aquisition_point = 0x40, epoch_start = 0x80 } state;
    enum class Palette_upd : uint_fast8_t { no, yes = 0x80 } palette_upd;
    Palette_ID  palette_id;
    uint_fast8_t sprite_cnt;
    PCS( In_file &f );
};

PCS::PCS( In_file &f )
{
    width           =               f.read_u2();
    height          =               f.read_u2();
    (void)                          f.read_u1();
    comp_id         = (Comp_ID)     f.read_u2();
    state           = (State)       f.read_u1();
    palette_upd     = (Palette_upd) f.read_u1();
    palette_id      = (Palette_ID)  f.read_u1();
    sprite_cnt      =               f.read_u1();
}

struct Sprite
{
    Obj_ID obj_id;
    Window_ID window_id;
    enum class Flag : uint_fast8_t { null, forced = 0x40 } flag;
    uint_fast16_t tgt_hpos, tgt_vpos, src_hpos, src_vpos, width, height;
    Sprite( In_file &f );
};

Sprite::Sprite( In_file &f )
{
    obj_id    = (Obj_ID)    f.read_u2();
    window_id = (Window_ID) f.read_u1();
    flag      = (Flag)      f.read_u1();
    tgt_hpos  =             f.read_u2();
    tgt_vpos  =             f.read_u2();
    src_hpos  =             f.read_u2();
    src_vpos  =             f.read_u2();
    width     =             f.read_u2();
    height    =             f.read_u2();
}

void sup_count_forced( In_file &f )
{
    uintmax_t n_comp = 0, n_forced = 0;
    while( f.more() ) {
        Segment seg( f );
        off_t next = f.pos() + seg.size;
        if( seg.type == Segment::Type::PCS ) {
            PCS pcs( f );
            n_comp += pcs.sprite_cnt;
            for( uint_fast8_t comp_idx = 0; comp_idx < pcs.sprite_cnt; ++comp_idx ) {
                Sprite spr( f );
                if( spr.flag == Sprite::Flag::forced ) ++n_forced;
            }
        }
        f.pos( next );
    }
    printf( "%ju %ju\n", n_forced, n_comp );
}

void sup_count_forced( In_file &&f )
{
    sup_count_forced( f );
}

int main( int argc, char **argv )
{
    if( argc > 1 )
        while( *++argv ) sup_count_forced( In_file( *argv ));
    else
        sup_count_forced( In_file( "stdin", stdin ));
}
