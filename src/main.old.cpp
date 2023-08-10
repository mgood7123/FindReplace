#include <stdexcept>
#include <iostream>

#include <cppfs/fs.h>
#include <cppfs/FileHandle.h>
#include <cppfs/FileIterator.h>

#include <composable_list.h>

#include <sstream>
#include <fstream>

// https://github.com/webmasterar/Unrestricted-Multi-Shift-And

#include "UnrestrictedMultiShiftAnd.h"

#include <memory>
#include <cstring>

#include <tmpfile.h>

#include <mmaptwo.hpp>

std::string search_item;
std::string replace_item;

const char * ASCII = " qwertyuiopasdfghjklzxcvbnmQWERTYUIOPASDFGHJKLZXCVBNM`~1!2@3#4$5%6^7&8*9(0)-_=+[{]}\\|;:'\",<.>/?\n\t\r\v";

class Searcher {
    public:

    UnrestrictedMultiShiftAnd * u_;

    Searcher(const std::string & search_pattern) {
        u_ = new UnrestrictedMultiShiftAnd(ASCII, true);
        u_->addPattern(search_pattern);
    }

    UnrestrictedMultiShiftAnd & u() {
        return *u_;
    }

    unsigned int end_idx(std::multimap<int, std::pair<int, int>> & matches, unsigned int idx) {
        return std::next(matches.begin(), idx)->first;
    }

    unsigned int begin_idx(std::multimap<int, std::pair<int, int>> & matches, unsigned int idx) {
        auto p = std::next(matches.begin(), idx);
        return p->first - p->second.second;
    }

    ~Searcher() {
        delete u_;
    }
};

std::shared_ptr<Searcher> searcher;

class MMap {
    public:

    enum DIR { FORWARDS, BACKWARDS };

    private:

    size_t chunk_length = 0;
    DIR direction = FORWARDS;
    size_t read_pos = 0;
    size_t write_pos = 0;

    // we may do     auto map = MMapHelper(path, 'r').map(0, 500);
    // map must extend the lifetime of the opened file descriptor held by mmaptwo_i to map new files
    //   we do not know what happens if the descriptor is closed before the map is unmapped, so best tie together
    //
    class MMapHelper {
                        /**
                        * \brief Destructor; closes the file.
                        * \note The destructor will not free any acquired pages!
                        */
        std::shared_ptr<mmaptwo::mmaptwo_i> allocated_file;
        bool open = false;
        bool zero_size = false;

        public:

        MMapHelper() = default;

        void error(std::exception const& e) {
            if (strstr(e.what(), "size of zero invalid") != nullptr) {
                zero_size = true;
                open = true;
                return;
            }
            std::cerr << "failed to open file:" << std::endl;
            std::cerr << "\t" << e.what() << std::endl;
            open = false;
        }

        MMapHelper(const char * path, char mode) {
            try {
                const char m[3] = {mode, 'e', '\0'};
                allocated_file = std::shared_ptr<mmaptwo::mmaptwo_i>(mmaptwo::open(path, m, 0, 0), [](auto p) { delete static_cast<mmaptwo::mmaptwo_i*>(p); });
                open = true;
            } catch (std::exception const& e) {
                error(e);
            }
        }

        MMapHelper(const unsigned char * path, char mode) {
            try {
                const char m[3] = {mode, 'e', '\0'};
                allocated_file = std::shared_ptr<mmaptwo::mmaptwo_i>(mmaptwo::u8open(path, m, 0, 0), [](auto p) { delete static_cast<mmaptwo::mmaptwo_i*>(p); });
                open = true;
            } catch (std::exception const& e) {
                error(e);
            }
        }

        MMapHelper(const wchar_t * path, char mode) {
            try {
                const char m[3] = {mode, 'e', '\0'};
                allocated_file = std::shared_ptr<mmaptwo::mmaptwo_i>(mmaptwo::wopen(path, m, 0, 0), [](auto p) { delete static_cast<mmaptwo::mmaptwo_i*>(p); });
                open = true;
            } catch (std::exception const& e) {
                error(e);
            }
        }

        const char * get_api() const {
            int os = mmaptwo::get_os();
            return os == mmaptwo::os_unix ? "mmap(2)" : os == mmaptwo::os_win32 ? "MapViewOfFile" : "(unknown api)";
        }

        std::shared_ptr<mmaptwo::page_i> obtain_map(size_t offset, size_t size) {
            return obtain_map_impl(offset, size, allocated_file);
        }

        bool is_open() const { return open; }

        size_t length() const {
            if (open && !zero_size) {
                return allocated_file->length();
            }
            return 0;
        }

        private:

        std::shared_ptr<mmaptwo::page_i> obtain_map_impl(size_t offset, size_t size, std::shared_ptr<mmaptwo::mmaptwo_i> allocated_file) {
            if (!open || zero_size) goto fail;
            try {
                mmaptwo::page_i * page = allocated_file->acquire(size, offset);
                if (!page) {
                    throw std::system_error(mmaptwo::get_errno(), std::generic_category());
                }
                // tie allocated file to page
                return std::shared_ptr<mmaptwo::page_i>(page, [allocated_file](auto page) { delete static_cast<mmaptwo::page_i*>(page); });
            } catch (std::exception const& e) {
                std::cerr << "failed to call api " << get_api() << " to bring file into memory:" << std::endl;
                std::cerr << "\t" << e.what() << std::endl;
            }
            fail:
            return std::shared_ptr<mmaptwo::page_i>(nullptr, [](auto){});
        }
    };

    MMapHelper read_map, write_map;

    std::shared_ptr<mmaptwo::page_i> read_page, write_page;

    template <typename C>
    void map_open(unsigned int chunk_length, C path, char mode, DIR direction) {
        this->chunk_length = chunk_length;
        this->direction = direction;
        if (mode == 'r') {
            read_map = MMapHelper(path, 'r');
            if (read_map.is_open()) {
                if (read_map.length() != 0) {
                    read_pos = direction == BACKWARDS ? read_map.length() : 0;
                }
            }
        } else if (mode == 'w') {
            write_map = MMapHelper(path, 'w');
            if (write_map.is_open()) {
                if (write_map.length() != 0) {
                    write_pos = direction == BACKWARDS ? write_map.length() : 0;
                }
            }
        }
    }

    public:

    MMap() = default;

    MMap(unsigned int chunk_length, const char * path, char mode, DIR direction) {
        map_open(chunk_length, path, mode, direction);
    }

    MMap(unsigned int chunk_length, const unsigned char * path, char mode, DIR direction) {
        map_open(chunk_length, path, mode, direction);
    }

    MMap(unsigned int chunk_length, const wchar_t * path, char mode, DIR direction) {
        map_open(chunk_length, path, mode, direction);
    }

    void seekg(unsigned int pos) {
        read_pos = pos;
    }

    void seekp(unsigned int pos) {
        write_pos = pos;
    }

    unsigned int leftg() {
        if (direction == FORWARDS) {
            return read_map.length() - read_pos;
        } else {
            return read_map.length() - (read_map.length() - read_pos);
        }
    }

    unsigned int leftp() {
        if (direction == FORWARDS) {
            return write_map.length() - write_pos;
        } else {
            return write_map.length() - (write_map.length() - write_pos);
        }
    }

    bool can_read_() {
        if (direction == FORWARDS) {
            return read_pos < read_map.length();
        } else {
            return read_pos != 0;
        }
    }

    bool can_write_() {
        if (direction == FORWARDS) {
            return write_pos < write_map.length();
        } else {
            return write_pos != 0;
        }
    }

    bool is_openg() {
        return read_map.is_open();
    }

    bool is_openp() {
        return write_map.is_open();
    }

    bool can_read() {
        if (!read_map.is_open()) {
            return false;
        }
        if (read_map.length() == 0) {
            return false;
        }
        if (!can_read_()) {
            return false;
        }
        return true;
    }

    bool can_write() {
        if (!write_map.is_open()) {
            return false;
        }
        if (!can_write_()) {
            return false;
        }
        return true;
    }

    struct State {
        char * buffer = nullptr;
        size_t buffer_length = 0;
        size_t chunk_length = 0;
    };

    private:
    State state;

    public:

    Composable_List<State> afterRead;

    void read() {
        while (can_read()) {
            read_impl();
            state.buffer = reinterpret_cast<char*>(read_page->get());
            state.buffer_length = read_page->length();
            state.chunk_length = chunk_length;
            afterRead.visitFromStart(state);
        }
    }

    private:
    void read_impl() {
        seekg(read_pos);
        int left = leftg();
        if (left == 0) {
            read_page.reset();
        } else {
            if (left > chunk_length*2) {
                if (direction == FORWARDS) {
                    read_page.reset();
                    read_page = read_map.obtain_map(read_pos, chunk_length*2);
                    seekg(read_pos + chunk_length);
                } else {
                    read_page.reset();
                    read_page = read_map.obtain_map(read_pos, chunk_length*2);
                    seekg(read_pos - chunk_length);
                }
            } else {
                if (direction == FORWARDS) {
                    read_page.reset();
                    read_page = read_map.obtain_map(read_pos, left);
                    seekg(read_pos + left);
                } else {
                    read_page.reset();
                    read_page = read_map.obtain_map(read_pos, left);
                    seekg(read_pos - left);
                }
            }
        }
    }
};

class MMapEmitter {

    MMap & map;

    public:

    MMapEmitter(MMap & map) : map(map) {}

    struct Info {
        unsigned int begin;
        unsigned int end;
        char * buffer;
    };

    Composable_List<Info> onMatch;
    Composable_List<Info> onNonMatch;
    Composable_List<Info> afterRead;
    
    void read() {
        unsigned int index = -1;

        map.afterRead.after([this, &index] (auto state) {

            auto b = state.buffer;
            auto bl = state.buffer_length;

            if (index != -1) {
                index -= bl - state.chunk_length;
            }

            unsigned int search_offset = index == -1 ? 0 : index+1;
            unsigned int search_length = bl - search_offset;
            {
                Info i;
                i.buffer = b;
                i.begin = search_offset;
                i.end = search_length;
                afterRead.visitFromStart(i);
            }
            if (searcher->u().search(b, search_offset, search_length)) {
                if (index == -1) {
                    index = 0;
                }

                auto matches = searcher->u().getMatches();

                auto idx_m = matches.size();

                unsigned int previous_end_index = 0;

                for (unsigned int idx = 0; idx < idx_m; idx++) {
                    auto start_index = searcher->begin_idx(matches, idx);
                    auto end_index = searcher->end_idx(matches, idx);
                    Info i;
                    i.buffer = b;
                    if (start_index != 0) {
                        i.begin = previous_end_index;
                        i.end = start_index;
                        if (i.begin != i.end) {
                            onNonMatch.visitFromStart(i);
                        }
                    }
                    i.begin = start_index;
                    i.end = end_index+1;
                    if (i.begin != i.end) {
                        onMatch.visitFromStart(i);
                    }
                    if (idx+1 == idx_m) {
                        i.begin = end_index+1;
                        i.end = bl;
                        if (i.begin != i.end) {
                            onNonMatch.visitFromStart(i);
                        }
                    }
                    previous_end_index = i.end;
                    index = std::max(index, end_index);
                }

                searcher->u().clearMatches();
            } else {
                index = -1;
                Info i;
                i.buffer = b;
                i.begin = search_offset;
                i.end = search_length;
                onNonMatch.visitFromStart(i);
            }

            return state;
        });

        map.read();
        map.seekg(0);
    }

    void seek(unsigned int pos) {
        map.seekg(pos);
    }
};

#include <regex>

void invokeMMAP(const char * path) {
    auto rep_len = replace_item.size();
    auto search_len = search_item.size();
    bool is_searching = rep_len == 0;
    bool replace_same_length = rep_len == search_len;
    bool replace_smaller = rep_len < search_len;
    bool replace_greater = rep_len > search_len;

    if (is_searching) {
        MMap m(searcher->u().getMaxPatternLength(), path, 'r', MMap::FORWARDS);
        if (m.is_openg() && m.leftg() == 0) {
            std::cout << "skipping zero length file: " << path << std::endl;
            return;
        }

        std::regex e(search_item, std::regex::ECMAScript | std::regex::optimize);

        m.afterRead.after([&e](auto info) {
            std::smatch wm;
            auto text = std::string(info.buffer, info.buffer_length);
            if(std::regex_search(text, wm, e)) {
                std::cout << "matches for buffer: ";
                std::cout.write(info.buffer, info.buffer_length);
                std::cout << std::endl;
                std::cout << "Prefix: '" << wm.prefix() << "'" << std::endl;
                for (size_t i = 0; i < wm.size(); ++i) 
                    std::cout << i << ": " << wm[i] << std::endl;
                std::cout << "Suffix: '" << wm.suffix() << "'" << std::endl << std::endl;
            } else {
                std::cout << "no matches for buffer: ";
                std::cout.write(info.buffer, info.buffer_length);
                std::cout << std::endl;
            }
            return info;
        });
        m.read();
        m.seekg(0);

        // std::string test = "hello 神谕 unicode"; // utf8
        // std::string expr = "[神-谕]+"; // utf8

        // std::regex e(expr);
        // std::smatch wm;
        // if(std::regex_search(test, wm, e)) {
        //     std::cout << wm.str(0) << '\n';
        // }

        // MMapEmitter emit(m);

        // emit.onMatch.after([&](auto info) {
        //     std::cout << "onMatch buffer: ";
        //     std::cout.write(info.buffer+info.begin, info.end-info.begin);
        //     std::cout << std::endl;
        //     return info;
        // });
        // emit.onNonMatch.after([&](auto info) {
        //     std::cout << "onNonMatch buffer: ";
        //     std::cout.write(info.buffer+info.begin, info.end-info.begin);
        //     std::cout << std::endl;
        //     return info;
        // });

        // emit.read();
        // emit.seek(0);
    }
}

struct Reader {
    unsigned int length = 0;
    unsigned int buffer_len = 0;

    char * buffer = nullptr;

    char * read_buffer = nullptr;
    unsigned int read_buffer_len = 0;

    char * look_ahead_buffer = nullptr;
    unsigned int look_ahead_buffer_len = 0;
    
    unsigned int read_pos = 0;

    unsigned int bytes_read = 0;

    enum DIR { FORWARDS, BACKWARDS };

    DIR direction = FORWARDS;

    Reader() = default;

    Reader(unsigned int buffer_len, unsigned int length, DIR direction) : direction(direction) {
        this->buffer_len = buffer_len;
        this->read_buffer_len = 0;
        this->length = length;

        if (buffer_len != 0) {
            buffer = new char[buffer_len*4];
            std::memset(buffer, 0, sizeof(char)*(buffer_len*4));
            read_buffer = buffer;
            look_ahead_buffer = &buffer[buffer_len];
            look_ahead_buffer_len = buffer_len;
            if (direction == BACKWARDS) {
                read_pos = length;
            }
        } else {
            buffer = nullptr;
            read_buffer = nullptr;
            look_ahead_buffer = nullptr;
        }
    }
    ~Reader() {
        if (buffer_len != 0) {
            delete[] buffer;
            read_buffer = nullptr;
            look_ahead_buffer = nullptr;
        }
    }

    void seek(unsigned int pos, std::istream & stream) {
        if (read_pos == pos) {
            read_pos = stream.tellg();
        }
        if (read_pos != pos) {
            if (pos >= 0 && pos <= length) {
                read_pos = pos;
                stream.seekg(pos);
            }
        }
    }

    unsigned int seek_pos() {
        return read_pos;
    }

    unsigned int left() {
        if (direction == FORWARDS) {
            return length - read_pos;
        } else {
            return length - (length - read_pos);
        }
    }

    bool can_read() {
        if (direction == FORWARDS) {
            return read_pos < length;
        } else {
            return read_pos != 0;
        }
    }

    char * get_buffer() {
        unsigned int l = buffer_len*2;
        std::memcpy(&buffer[l], read_buffer, read_buffer_len);
        std::memcpy(&buffer[l+read_buffer_len], &look_ahead_buffer[buffer_len - look_ahead_buffer_len], look_ahead_buffer_len);
        return &buffer[l];
    }

    unsigned int get_buffer_len() {
        return read_buffer_len + look_ahead_buffer_len;
    }

    void print() {
        std::cout << "Reader: " << "direction = " << (direction == FORWARDS ? "Forwards" : "Backwards") << std::endl;
        std::cout << "Reader: " << "can read = " << (can_read() ? "true" : "false") << std::endl;
        std::cout << "Reader: " << "length = " << std::to_string(length) << std::endl;
        std::cout << "Reader: " << "read_pos = " << std::to_string(read_pos) << std::endl;
        std::cout << "Reader: " << "left = " << std::to_string(left()) << std::endl;
        if (read_buffer_len == 0) {
            std::cout << "Reader: " << "read buffer length = 0" << std::endl;
            std::cout << "Reader: " << "read buffer = nullptr" << std::endl;
        } else {
            std::cout << "Reader: " << "read buffer length = " << std::to_string(read_buffer_len) << std::endl;
            std::cout << "Reader: " << "read buffer = ";
            std::cout.write(read_buffer, read_buffer_len);
            std::cout << std::endl;
        }
        if (look_ahead_buffer_len == 0) {
            std::cout << "Reader: " << "look ahead buffer length = 0" << std::endl;
            std::cout << "Reader: " << "look ahead buffer = nullptr" << std::endl;
        } else {
            std::cout << "Reader: " << "look ahead buffer length = " << std::to_string(look_ahead_buffer_len) << std::endl;
            std::cout << "Reader: " << "look ahead buffer = ";
            std::cout.write(&look_ahead_buffer[buffer_len - look_ahead_buffer_len], look_ahead_buffer_len);
            std::cout << std::endl;
        }

        if (get_buffer_len() == 0) {
            std::cout << "Reader: " << "buffer length = 0" << std::endl;
            std::cout << "Reader: " << "buffer = nullptr" << std::endl;
        } else {
            std::cout << "Reader: " << "buffer length = " << std::to_string(get_buffer_len()) << std::endl;
            std::cout << "Reader: " << "buffer = ";
            std::cout.write(get_buffer(), get_buffer_len());
            std::cout << std::endl;
        }
            std::cout << std::endl;
    }

    void read(std::istream & stream) {
        seek(read_pos, stream);
        if (direction == FORWARDS) {
            int l = left();
            if (l == 0) {
                read_buffer_len = 0;
                look_ahead_buffer_len = 0;
            } else if (l > buffer_len) {
                read_buffer_len = buffer_len;
                look_ahead_buffer_len = read_buffer_len;
                std::memmove(read_buffer, look_ahead_buffer, buffer_len);
                stream.read(look_ahead_buffer, look_ahead_buffer_len);
                bytes_read = look_ahead_buffer_len;
                seek(read_pos + look_ahead_buffer_len, stream);
            } else {
                read_buffer_len = look_ahead_buffer_len;
                look_ahead_buffer_len = l;
                std::memmove(read_buffer, look_ahead_buffer, buffer_len);
                stream.read(&look_ahead_buffer[buffer_len - look_ahead_buffer_len], look_ahead_buffer_len);
                bytes_read = look_ahead_buffer_len;
                seek(read_pos + look_ahead_buffer_len, stream);
            }
        } else {
            int l = left();
            if (l == 0) {
                read_buffer_len = 0;
                look_ahead_buffer_len = 0;
            } else if (l > buffer_len) {
                read_buffer_len = buffer_len;
                look_ahead_buffer_len = buffer_len;
                seek(read_pos - read_buffer_len, stream);
                std::memmove(look_ahead_buffer, read_buffer, buffer_len);
                stream.read(read_buffer, read_buffer_len);
                bytes_read = read_buffer_len;
            } else {
                read_buffer_len = l;
                look_ahead_buffer_len = buffer_len;
                seek(read_pos - read_buffer_len, stream);
                std::memmove(look_ahead_buffer, read_buffer, buffer_len);
                stream.read(read_buffer, read_buffer_len);
                bytes_read = read_buffer_len;
            }
        }
    }
};

struct Writer {
    unsigned int length = 0;
    unsigned int buffer_len = 0;

    unsigned int write_pos = 0;
    unsigned int bytes_written = 0;

    enum DIR { FORWARDS, BACKWARDS };

    DIR direction = FORWARDS;

    Writer() = default;

    Writer(unsigned int buffer_len, unsigned int length, DIR direction) : direction(direction) {
        this->buffer_len = buffer_len;
        this->length = length;

        if (buffer_len != 0) {
            if (direction == BACKWARDS) {
                write_pos = length;
            }
        }
    }
    ~Writer() {
        if (buffer_len != 0) {
        }
    }

    void seek(unsigned int pos, std::ostream & stream) {
        if (write_pos == pos) {
            write_pos = stream.tellp();
        }
        if (write_pos != pos) {
            if (pos >= 0 && pos <= length) {
                write_pos = pos;
                stream.seekp(pos);
            }
        }
    }

    unsigned int seek_pos() {
        return write_pos;
    }

    unsigned int left() {
        if (direction == FORWARDS) {
            return length - write_pos;
        } else {
            return length - (length - write_pos);
        }
    }

    bool can_write() {
        if (direction == FORWARDS) {
            return write_pos < length;
        } else {
            return write_pos != 0;
        }
    }

    void print() {
        std::cout << "Writer: " << "direction = " << (direction == FORWARDS ? "Forwards" : "Backwards") << std::endl;
        std::cout << "Writer: " << "can write = " << (can_write() ? "true" : "false") << std::endl;
        std::cout << "Reader: " << "length = " << std::to_string(length) << std::endl;
        std::cout << "Reader: " << "write_pos = " << std::to_string(write_pos) << std::endl;
        std::cout << "Reader: " << "left = " << std::to_string(left()) << std::endl;
        std::cout << std::endl;
    }

    void write(std::ostream & stream, const char * buffer, unsigned int len) {
        seek(write_pos, stream);
        stream.write(buffer, len);
        bytes_written = len;
        if (direction == FORWARDS) {
            seek(write_pos + len, stream);
        } else {
            seek(write_pos - len, stream);
        }
    }
};

struct ReadingSearcher {
    Reader r;

    struct State {
        char * buffer;
        unsigned int buffer_length;
    };

    State state;

    ReadingSearcher(unsigned int buffer_len, unsigned int length, Reader::DIR direction) : r(buffer_len, length, direction) {}
    
    bool can_read() {
        return r.can_read();
    }

    Composable_List<State> afterRead;

    void read(std::iostream & mem) {
        r.read(mem);
        state.buffer = r.get_buffer();
        state.buffer_length = r.get_buffer_len();
        afterRead.visitFromStart(state);
    }

    void seek(unsigned int pos, std::iostream & mem) {
        r.seek(pos, mem);
    }
};

class Emitter {

    ReadingSearcher rs;

    public:

    Emitter(unsigned int buffer_len, unsigned int length, Reader::DIR direction) : rs(buffer_len, length, direction) {}

    struct Info {
        unsigned int begin;
        unsigned int end;
        char * buffer;
        unsigned int stream_pos_begin;
    };

    Composable_List<Info> onMatch;
    Composable_List<Info> onNonMatch;
    Composable_List<Info> afterRead;

    void read(std::iostream & mem) {
        unsigned int index = -1;

        rs.afterRead.after([this, &index] (auto state) {

            auto b = state.buffer;
            auto bl = state.buffer_length;

            if (index != -1) {
                index -= bl - searcher->u().getMaxPatternLength();
            }

            unsigned int search_offset = index == -1 ? 0 : index+1;
            unsigned int search_length = bl - search_offset;
            {
                Info i;
                i.buffer = b;
                i.begin = search_offset;
                i.end = search_length;
                i.stream_pos_begin = rs.r.seek_pos() - rs.r.bytes_read;
                afterRead.visitFromStart(i);
            }
            if (searcher->u().search(b, search_offset, search_length)) {
                if (index == -1) {
                    index = 0;
                }

                // std::cout << "match found for pattern \"" << search_item << "\" in the following buffer" << std::endl;
                // std::cout.write(b, bl);
                // std::cout << std::endl << std::endl;

                auto matches = searcher->u().getMatches();

                auto idx_m = matches.size();

                unsigned int previous_end_index = 0;

                for (unsigned int idx = 0; idx < idx_m; idx++) {
                    auto start_index = searcher->begin_idx(matches, idx);
                    auto end_index = searcher->end_idx(matches, idx);
                    // std::cout << "buffer[start_index /* " << std::to_string(start_index) << " */] = " << b[start_index] << std::endl;
                    // std::cout << "buffer[end_index   /* " << std::to_string(end_index) << " */] = " << b[end_index] << std::endl;
                    Info i;
                    i.buffer = b;
                    if (start_index != 0) {
                        i.begin = previous_end_index;
                        i.end = start_index;
                        if (i.begin != i.end) {
                            i.stream_pos_begin = rs.r.seek_pos() - rs.r.bytes_read;
                            onNonMatch.visitFromStart(i);
                        }
                    }
                    i.begin = start_index;
                    i.end = end_index+1;
                    if (i.begin != i.end) {
                        i.stream_pos_begin = rs.r.seek_pos() - rs.r.bytes_read;
                        onMatch.visitFromStart(i);
                    }
                    if (idx+1 == idx_m) {
                        i.begin = end_index+1;
                        i.end = bl;
                        if (i.begin != i.end) {
                            i.stream_pos_begin = rs.r.seek_pos() - rs.r.bytes_read;
                            onNonMatch.visitFromStart(i);
                        }
                    }
                    previous_end_index = i.end;
                    index = std::max(index, end_index);
                }

                searcher->u().clearMatches();
            } else {
                // std::cout << "match not found for pattern \"" << search_item << "\" in the following buffer" << std::endl;
                // std::cout.write(b, bl);
                // std::cout << std::endl << std::endl;
                index = -1;
                Info i;
                i.buffer = b;
                i.begin = search_offset;
                i.end = search_length;
                onNonMatch.visitFromStart(i);
            }

            return state;
        });

        while (rs.can_read()) {
            rs.read(mem);
        }
        rs.seek(0, mem);
    }

    void seek(unsigned int pos, std::iostream & mem) {
        rs.seek(pos, mem);
    }

    Writer writer() {
        return Writer(rs.r.buffer_len, rs.r.length, rs.r.direction == Reader::FORWARDS ? Writer::FORWARDS : Writer::BACKWARDS);
    }

    Writer writer(Writer::DIR direction) {
        return Writer(rs.r.buffer_len, rs.r.length, direction);
    }
};

unsigned int compute_additional_space(std::iostream & mem, unsigned int s, unsigned int rep_len) {

    Emitter emit(searcher->u().getMaxPatternLength(), s, Reader::FORWARDS);
    
    unsigned int space = 0;

    emit.onMatch.after([&space, rep_len](auto info) { space += rep_len; return info; });
    emit.onMatch.after([](auto info) { std::cout << "onMatch: "; for(auto i = info.begin; i < info.end; i++) std::cout << info.buffer[i]; std::cout << std::endl; return info; });
    emit.onNonMatch.after([](auto info) { std::cout << "onNonMatch: "; for(auto i = info.begin; i < info.end; i++) std::cout << info.buffer[i]; std::cout << std::endl; return info; });

    emit.read(mem);
    emit.seek(0, mem);

    return space;
}

void invoke2(std::iostream & mem, unsigned int s) {
    unsigned int index = -1;

    auto rep_len = replace_item.size();
    auto search_len = search_item.size();
    bool is_searching = rep_len == 0;
    bool replace_same_length = rep_len == search_len;
    bool replace_smaller = rep_len < search_len;
    bool replace_greater = rep_len > search_len;

    if (is_searching) {
        Emitter emit(searcher->u().getMaxPatternLength(), s, Reader::FORWARDS);
        emit.onMatch.after([](auto info) { std::cout << "onMatch: "; for(auto i = info.begin; i < info.end; i++) std::cout << info.buffer[i]; std::cout << std::endl; return info; });
        emit.read(mem);
    } else {
        if (replace_smaller) {
            auto cs = s + compute_additional_space(mem, s, rep_len - search_len);
            std::cout << "computed space: " << std::to_string(cs) << ", original space: " << std::to_string(s) << std::endl;
        } else if (replace_same_length) {
            std::cout << "computed space: " << std::to_string(s) << ", original space: " << std::to_string(s) << std::endl;

            Emitter emit(searcher->u().getMaxPatternLength(), s, Reader::FORWARDS);
            emit.onMatch.after([&](auto info) {
                std::cout << "info.begin: " << std::to_string(info.begin) << std::endl;
                std::cout << "info.end: " << std::to_string(info.end) << std::endl;
                std::cout << "onMatch buffer: ";
                std::cout.write(info.buffer+info.begin, info.end-info.begin);
                std::cout << std::endl;
                // std::cout << "onMatch stream: ";
                // for(auto i = info.begin; i < info.end; i++) {
                //     char buf;
                //     auto p = mem.tellg();
                //     mem.seekg(info.stream_pos_begin+(i - (info.begin - (info.end - info.begin))));
                //     mem.read(&buf, 1);
                //     mem.seekg(p);
                //     std::cout << buf;
                // }
                // std::cout << std::endl;
                return info;
            });
            emit.read(mem);
            emit.seek(0, mem);

            Emitter emit2(searcher->u().getMaxPatternLength(), s, Reader::FORWARDS);
            emit2.onMatch.after([&](auto info) {
                std::cout << "onMatch buffer S: ";
                std::cout.write(info.buffer+info.begin, info.end-info.begin);
                std::cout << std::endl;
                std::cout << "onMatch stream S: ";
                unsigned int len = info.end - (info.end - info.begin);
                unsigned int s = info.stream_pos_begin;
                unsigned int e = info.stream_pos_begin + 2;
                for(auto i = s; i != e; i++) {
                    char buf;
                    auto p = mem.tellg();
                    mem.seekg(i);
                    mem.read(&buf, 1);
                    mem.seekg(p);
                    std::cout << buf;
                }
                std::cout << std::endl;
                return info;
            });
            emit2.afterRead.after([&] (auto info) {
                std::cout << "afterRead buffer S: ";
                std::cout.write(info.buffer+info.begin, info.end-info.begin);
                std::cout << std::endl;
                return info;
            });
            emit2.read(mem);
            emit2.seek(0, mem);

        } else if (replace_greater) {
            auto cs = s + compute_additional_space(mem, s, rep_len - search_len);
            std::cout << "computed space: " << std::to_string(cs) << ", original space: " << std::to_string(s) << std::endl;

        }
    }
}

std::stringstream * toMemFile(std::istream * inHandle, unsigned int s, unsigned int buffer_length) {
    std::cout << "copying file to memory file" << std::endl;
    std::stringstream * memFile = new std::stringstream(std::ios::binary | std::ios::in | std::ios::out);
    unsigned int s_r = 0;
    unsigned int s_w = 0;

    char buffer[buffer_length];
    unsigned int s_c = s > buffer_length ? buffer_length : s;
    while (!inHandle->eof()) {
        inHandle->read(buffer, s_c);
        if (inHandle->gcount() == 0) {
            break;
        }
        s_r += inHandle->gcount();
        memFile->write(buffer, inHandle->gcount());
        memFile->flush();
        std::cout << "wrote " << inHandle->gcount() << " bytes" << std::endl;
        s_w += inHandle->gcount();
        if (s_r == s) {
            std::cout << s_r << " == " << s << std::endl;
            break;
        } else {
            unsigned int ss = s - s_r;
            s_c = ss > buffer_length ? buffer_length : ss;
        }
        if (!inHandle->eof() && (inHandle->bad() || inHandle->fail())) {
            std::cout << "failed to read " << s_c << " bytes (" << s_r << " of " << s << " bytes)" << std::endl;
            break;
        }
    }
    std::cout << "copied file to memory file" << std::endl;
    return memFile;
}

void toOutStream(std::istream * inHandle, std::ostream * outHandle, unsigned int s, unsigned int buffer_length) {
    std::cout << "copying file to out stream" << std::endl;
    unsigned int s_r = 0;
    unsigned int s_w = 0;

    char buffer[buffer_length];
    unsigned int s_c = s > buffer_length ? buffer_length : s;
    while (!inHandle->eof()) {
        inHandle->read(buffer, s_c);
        if (inHandle->gcount() == 0) {
            break;
        }
        s_r += inHandle->gcount();
        outHandle->write(buffer, inHandle->gcount());
        outHandle->flush();
        std::cout << "wrote " << inHandle->gcount() << " bytes" << std::endl;
        s_w += inHandle->gcount();
        if (s_r == s) {
            std::cout << s_r << " == " << s << std::endl;
            break;
        } else {
            unsigned int ss = s - s_r;
            s_c = ss > buffer_length ? buffer_length : ss;
        }
        if (!inHandle->eof() && (inHandle->bad() || inHandle->fail())) {
            std::cout << "failed to read " << s_c << " bytes (" << s_r << " of " << s << " bytes)" << std::endl;
            break;
        }
    }
    std::cout << "copied file to out stream" << std::endl;
}

void in_place_search(std::iostream * stream, unsigned int s, unsigned int buffer_length) {
    unsigned int s_r = 0;

    char buffer[buffer_length];

    unsigned int s_c = s > buffer_length ? buffer_length : s;

    while (true) {
        if (stream->read(buffer, s_c)) {
            s_c = stream->gcount();
            s_r += s_c;

            if (searcher->u().search(buffer, 0, s_c)) {
                std::cout << "match found for pattern \"" << search_item << "\" in the following buffer" << std::endl;
                std::cout.write(buffer, s_c);
                std::cout << std::endl << std::endl;
                auto matches = searcher->u().getMatches();

                auto idx_m = matches.size();

                std::cout << "matches size " << idx_m << std::endl;

                for (unsigned int idx = 0; idx < idx_m; idx++) {
                    auto begin_index = searcher->begin_idx(matches, idx);
                    std::cout << "buffer[begin_index /* " << begin_index << " */] = " << buffer[begin_index] << std::endl;

                    auto end_index = searcher->end_idx(matches, idx);
                    std::cout << "buffer[end_index   /* " << end_index << " */] = " << buffer[end_index] << std::endl;
                }

                searcher->u().clearMatches();
            }

            if (s_r == s) {
                std::cout << s_r << " == " << s << std::endl;
                break;
            } else {
                unsigned int ss = s - s_r;
                s_c = ss > buffer_length ? buffer_length : ss;
            }
        } else {
            std::cout << "failed to read " << buffer_length << " bytes (" << s_r << " of " << s << " bytes)" << std::endl;
            break;
        }
    }
}

void lsdir(const std::string & path)
{
    cppfs::FileHandle handle = cppfs::fs::open(path);

    if (!handle.exists()) {
        std::cout << "item does not exist:  " << path << std::endl;
        return;
    }

    if (handle.isDirectory())
    {
        // std::cout << "entering directory:  " << path << std::endl;
        for (cppfs::FileIterator it = handle.begin(); it != handle.end(); ++it)
        {
            lsdir(path + "/" + *it);
        }
        // std::cout << "leaving directory:  " << path << std::endl;
    } else if (handle.isFile()) {
        std::cout << "reading file:  " << path << std::endl;

        invokeMMAP(path.c_str());

        std::cout << "read file:  " << path << std::endl;

        exit(0);
    } else {
        std::cout << "unknown type:  " << path << std::endl;
    }
}

std::string unescape(const std::string& s)
{
  std::string res;
  std::string::const_iterator it = s.begin();
  while (it != s.end())
  {
    char c = *it++;
    if (c == '\\' && it != s.end())
    {
      char cc = *it++;
      switch (cc) {
      case '\\': c = '\\'; break;
      case 'n': c = '\n'; break;
      case 't': c = '\t'; break;
      case 'r': c = '\r'; break;
      case 'v': c = '\v'; break;
      default:
        res += '\\';
        res += cc;
        continue;
      }
    }
    res += c;
  }

  return res;
}

#ifdef _WIN32
#include <io.h> // _setmode()
#include <fcntl.h> // O_BINARY
#define REOPEN_STDIN_AS_BINARY() _setmode(_fileno(stdin), O_BINARY)
#else
#define REOPEN_STDIN_AS_BINARY() freopen(NULL, "rb", stdin)
#endif

#define GET_STDIN_SIZE(size_name) \
   unsigned int size_name = 0; \
   while (true) { \
      char tmp[1]; \
      unsigned int r = fread(tmp, 1, 1, stdin); \
      if (r == 0) break; \
      size_name += r; \
   }

void help() {
    puts("FindReplace dir/file/--stdin -s search_items [-r replacement]");
    puts("FindReplace dir/file/--stdin search_item [replacement]");
    puts("");
    puts("no arguments       this help text");
    puts("-h, --help         this help text");
    puts("dir/file           REQUIRED: the directory/file to search, CANNOT BE SPECIFIED WITH --stdin");
    puts("--stdin            REQUIRED: use stdin instead of a directory/file");
    puts("-s search_items    REQUIRED: the items to search for");
    puts("-r replacement     OPTIONAL: the item to replace with");
    puts("      |");
    puts("      |  CONSTRAINTS:");
    puts("      |");
    puts("      |  -r is required if -s is given");
    puts("      |");
    puts("      |   the following are equivilant");
    puts("      |    \\");
    puts("      |     |- FindReplace ... item_A item_B -r item_C");
    puts("      |     |   \\");
    puts("      |     |    searches for item_A and item_B and replaces with item_C");
    puts("      |     |");
    puts("      |     |- FindReplace ... -s item_A item_B -r item_C");
    puts("      |         \\");
    puts("      |          searches for item_A and item_B and replaces with item_C");
    puts("      |");
    puts("      |");
    puts("      |   the following are equivilant");
    puts("      |    \\");
    puts("      |     |- FindReplace ... item_A item_B");
    puts("      |     |   \\");
    puts("      |     |    searches for item_A and replaces with item_B");
    puts("      |     |");
    puts("      |     |- FindReplace ... item_A -r item_B");
    puts("      |     |   \\");
    puts("      |     |    searches for item_A and replaces with item_B");
    puts("      |     |");
    puts("      |     |- FindReplace ... -s item_A -r item_B");
    puts("      |         \\");
    puts("      |          searches for item_A and replaces with item_B");
    puts("      |");
    puts("      |");
    puts("      |   the following are NOT equivilant");
    puts("      |    \\");
    puts("      |     |- FindReplace ... -s item_A item_B");
    puts("      |     |   \\");
    puts("      |     |    searches for item_A and item_B");
    puts("      |     |");
    puts("      |     |- FindReplace ... -s item_A -r item_B");
    puts("      |         \\");
    puts("      |          searches for item_A and replaces with item_B");
    puts("      |");
    puts("      |");
    puts("      |   the following are NOT equivilant");
    puts("      |    \\");
    puts("      |     |- FindReplace ... -s item_A item_B");
    puts("      |     |   \\");
    puts("      |     |    searches for item_A and item_B");
    puts("      |     |");
    puts("      |     |- FindReplace ... item_A -r item_B");
    puts("      |         \\");
    puts("      |          searches for item_A and replaces with item_B");
    puts("      |__________________________________________________________________");
    puts("");
    puts("");
    puts("EXAMPLES:");
    puts("");
    puts("FindReplace --stdin \"\\n\" \"\\r\\n\"");
    puts("   searches 'stdin' for the item '\\n' (unix) and replaces it with '\\r\\n' (windows)");
    puts("");
    puts("FindReplace my_file \"\\n\" \"\\r\\n\"");
    puts("   searches 'my_file' for the item '\\n' (unix) and replaces it with '\\r\\n' (windows)");
    puts("");
    puts("FindReplace my_dir \"\\n\" \"\\r\\n\"");
    puts("   searches 'my_dir' recursively for the item '\\n' (unix) and replaces it with '\\r\\n' (windows)");
    puts("");
    puts("FindReplace --stdin apple");
    puts("   searches 'stdin' for the item 'apple'");
    puts("");
    puts("FindReplace --stdin \"apple pies\" \"pie kola\"");
    puts("   searches 'stdin' for the item 'apple pies', and replaces it with 'pie kola'");
    puts("");
    puts("FindReplace --stdin -s a foo \"go to space\"");
    puts("   searches 'stdin' for the items 'a', 'foo', and 'go to space'");
    puts("");
    puts("FindReplace --stdin -s a \"foo \\n bar\" go -r Alex");
    puts("   searches 'stdin' for the items 'a', 'foo \\n bar', and 'go', and replaces all of these with 'Alex'");
    exit(1);
}

int
#ifdef _WIN32
wmain(int argc, const wchar_t *argv[])
#else
main(int argc, const char*argv[])
#endif
{
    if (argc < 3) {
        help();
    }
    const char * dir = argv[1];
    if (strcmp(dir, "-h") == 0) {
        help();
    }
    if (strcmp(dir, "--help") == 0) {
        help();
    }
    search_item = unescape(argv[2]);
    replace_item = argc == 4 ? unescape(argv[3]) : "";

    searcher = std::make_shared<Searcher>(search_item);

    if (strcmp(dir, "--stdin") == 0) {
        std::cout << "using stdin as search area" << std::endl;
        std::cout << "searching for:        " << search_item << std::endl;
        if (replace_item.size() != 0) {
            std::cout << "replacing with:       " << replace_item << std::endl;
        }

        REOPEN_STDIN_AS_BINARY();

        TempFile tmp_file("tmp_stdin");

        std::cout << "created temporary file: " << tmp_file.get_path() << std::endl;

        std::ofstream std__in__file (tmp_file.get_path(), std::ios::binary | std::ios::out);

        toOutStream(&std::cin, &std__in__file, -1, 4096);

        std__in__file.flush();
        std__in__file.close();

        invokeMMAP(tmp_file.get_path().c_str());

        return 0;
    } else {
        std::cout << "directory/file to search:  " << dir << std::endl;
        std::cout << "searching for:        " << search_item << std::endl;
        if (replace_item.size() != 0) {
            std::cout << "replacing with:       " << replace_item << std::endl;
        }
        lsdir(dir);
    }
    return 0;
}