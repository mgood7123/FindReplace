#include <cppfs/fs.h>
#include <cppfs/FileHandle.h>
#include <cppfs/FileIterator.h>

#include <sstream>
#include <fstream>

#include <memory>
#include <cstring>

#include <mmap_iterator.h>

#include <tmpfile.h>

#ifdef _WIN32
#include <fileapi.h>
#else
#include <unistd.h>
#include <sys/types.h>
#endif

bool dry_run = false;
bool no_detach = false;
bool print_non_matches = false;
bool print_lines = false;
bool ignore_case = false;

struct SearchInfo {
    std::vector<std::string> s;
    std::string r;
} search_info;

#include <regex>

std::string print_escaped(const std::string& s)
{
  std::string x;
    if (s.size() != 0) {
        for (int i = 0, m = s.size()-1; i <= m; i++) {
            const char c = s[i];
            if (c == '\n') x += "\\n";
            else if (c == '\t') x += "\\t";
            else if (c == '\r') x += "\\r";
            else if (c == '\v') x += "\\v";
            else if (c == '\b') x += "\\b";
            else if (c == '\\') x += "\\\\";
            else x += c;
        }
    }
    std::cout << "const char x [" << std::to_string(x.size()+1) << "] = {";
    if (x.size() != 0) {
        for (int i = 0, m = x.size()-1; i <= m; i++) {
            std::cout << " '";
            const char c = x[i];
            if (c == '\\') std::cout << "\\\\";
            else std::cout << c;
            std::cout << "',";
        }
    }
    std::cout << " '\\0' };" << std::endl;
  return x;
}

std::string unescape(const std::string& s, bool unescape_regex, bool unescape_regex_replace)
{
    //print_escaped(s);
  std::string x;
    if (s.size() != 0) {
        bool slash = false;
        for (int i = 0, m = s.size()-1; i <= m; i++) {
            const char c = s[i];
            if (slash) {
                if (c == '\\') x += '\\';
                else if (c == 't') x += '\t';
                else if (c == 'r') x += '\r';
                else if (c == 'v') x += '\v';
                else if (c == 'b') x += '\b';
                else {
                    // unknown escape
                    x += '\\';
                    x += c;
                }
                slash = false;
            } else {
                if ((unescape_regex || unescape_regex_replace) && c == '\\') slash = true;
                else if (unescape_regex && c == '$') x += "\\$";
                else if (unescape_regex && c == '|') x += "\\|";
                else if (unescape_regex && c == '^') x += "\\^";
                else if (unescape_regex && c == '.') x += "\\.";
                else if (unescape_regex && c == '+') x += "\\+";
                else if (unescape_regex && c == '-') x += "\\-";
                else if (unescape_regex && c == '?') x += "\\?";
                else if (unescape_regex && c == '*') x += "\\*";
                else if (unescape_regex && c == '(') x += "\\(";
                else if (unescape_regex && c == ')') x += "\\)";
                else if (unescape_regex && c == '{') x += "\\{";
                else if (unescape_regex && c == '}') x += "\\}";
                else if (unescape_regex && c == '[') x += "\\[";
                else if (unescape_regex && c == ']') x += "\\]";
                else if (unescape_regex_replace && c == '$') x += "$$";
                else x += c;
            }
        }
        if (slash) {
            x += "\\\\";
        }
    }
    //print_escaped(x);
  return x;
}

template <typename BiDirIt>
struct RegexMatcher {

    struct SubMatch {
        std::string string;
        BiDirIt begin, end;
        std::sub_match<BiDirIt> sub_match;
        bool is_string;
        bool is_sub_match;

        SubMatch() : is_string(false), is_sub_match(false) {}
        SubMatch(std::string & string) : string(string), is_string(true), is_sub_match(false) {}
        SubMatch(BiDirIt begin, BiDirIt end) : begin(begin), end(end), is_string(false), is_sub_match(false) {}
        SubMatch(std::sub_match<BiDirIt> sub_match) : sub_match(sub_match), is_string(false), is_sub_match(true) {}

        friend std::ostream & operator << (std::ostream & os, SubMatch & o) {
            return o.is_sub_match ? os << o.sub_match : o.is_string ? os << o.string : os << std::string(o.begin, o.end);
        }
    };

    std::function<void(RegexMatcher<BiDirIt> * instance, SubMatch data)> onMatch = [](RegexMatcher<BiDirIt> * instance, SubMatch data) {}, onNonMatch = [](RegexMatcher<BiDirIt> * instance, SubMatch data) {};

    void search(BiDirIt begin, BiDirIt end, std::match_results<BiDirIt> current, std::match_results<BiDirIt> prev, std::regex regex) {
        search_ref(begin, end, current, prev, regex);
    }

    void search_ref(BiDirIt & begin, BiDirIt & end, std::match_results<BiDirIt> & current, std::match_results<BiDirIt> & prev, std::regex & regex) {
        while(std::regex_search(begin, end, current, regex)) {
            prev = current;
            if (current.size() != 0) {
                auto n = current.prefix();
                if (n.length() != 0) {
                    onNonMatch(this, n);
                }
            }
            for (size_t i = 0; i < current.size(); ++i) {
                auto & n = current[i];
                if (n.length() != 0) {
                    onMatch(this, n);
                }
            }

            begin = std::next(begin, current.position() + current.length());
        }
        if (prev.size() != 0) {
            auto n = prev.suffix();
            if (n.length() != 0) {
                onNonMatch(this, n);
            }
        } else {
            if (begin != end) {
                onNonMatch(this, {begin, end});
            }
        }
    }
};

template <typename BiDirIt>
struct RegexMatcherWithLineInfo : public RegexMatcher<BiDirIt> {

    using SubMatch = typename RegexMatcher<BiDirIt>::SubMatch;

    int64_t line = 0;

    std::function<void(RegexMatcher<BiDirIt> * instance, SubMatch data)> onMatch = [](RegexMatcher<BiDirIt> * instance, SubMatch data) {}, onNonMatch = [](RegexMatcher<BiDirIt> * instance, SubMatch data) {};
    std::function<void(RegexMatcher<BiDirIt> * instance, uint64_t line)> onPrintLine = [](RegexMatcher<BiDirIt> * instance, uint64_t line) {};

    private:

    std::string accumulation;
    bool was_on_match = false;
    bool needs_reset = true;
    bool line_has_match = false;

    std::vector<std::pair<std::function<void(RegexMatcher<BiDirIt> * instance, SubMatch data)>, SubMatch>> matches;

    void reset(RegexMatcher<BiDirIt> * instance, std::function<void(RegexMatcher<BiDirIt> * instance, SubMatch data)> & match_func) {
        if (print_lines && (print_non_matches || line_has_match)) {
            if (matches.size() != 0) {
                onPrintLine(instance, line);
                for (auto & p : matches) {
                    p.first(instance, p.second);
                }
                if (accumulation.size() != 0) {
                    match_func(instance, accumulation);
                }
            } else {
                if (accumulation.size() != 0) {
                    onPrintLine(instance, line);
                    match_func(instance, accumulation);
                }
            }
        }
        line++;
        if (!print_lines && (print_non_matches || line_has_match)) {
            if (accumulation.size() != 0) {
                onPrintLine(instance, line);
                match_func(instance, accumulation);
            }
        }
        accumulation = "";
        matches = std::move(std::vector<std::pair<std::function<void(RegexMatcher<BiDirIt> * instance, SubMatch data)>, SubMatch>>());
    }
    void process(RegexMatcher<BiDirIt> * instance, std::function<void(RegexMatcher<BiDirIt> * instance, SubMatch data)> & match_func, std::function<void(RegexMatcher<BiDirIt> * instance, SubMatch data)> & match_func_opposite, bool from_on_match, const char c) {
        if (was_on_match != from_on_match && accumulation.size() != 0) {
            if (accumulation.size() != 0) {
                if (print_lines) {
                    matches.push_back({match_func_opposite, accumulation});
                } else {
                    match_func_opposite(instance, accumulation);
                }
            }
            accumulation = "";
            was_on_match = from_on_match;
        }
        if (needs_reset) {
            reset(instance, match_func);
            needs_reset = false;
            line_has_match = false;
        }
        accumulation.push_back(c);
        if (c == '\n') {
            needs_reset = true;
        }
        if (from_on_match) line_has_match = true;
    }
    void flush(RegexMatcher<BiDirIt> * instance, SubMatch & match, bool from_on_match) {
        auto match_func = from_on_match ? onMatch : onNonMatch;
        auto match_func_opposite = !from_on_match ? onMatch : onNonMatch;
        if (match.is_sub_match) {
            auto str = match.sub_match.str();
            for (auto c : str) {
                process(instance, match_func, match_func_opposite, from_on_match, c);
            }
        } else {
            for (BiDirIt begin = match.begin; begin != match.end; begin++) {
                process(instance, match_func, match_func_opposite, from_on_match, *begin);
            }
        }
        was_on_match = from_on_match;
    }

    public:

    RegexMatcherWithLineInfo() {
        RegexMatcher<BiDirIt>::onMatch = [](RegexMatcher<BiDirIt> * instance, SubMatch match) {
            static_cast<RegexMatcherWithLineInfo<BiDirIt>*>(instance)->flush(instance, match, true);
        };
        RegexMatcher<BiDirIt>::onNonMatch = [](RegexMatcher<BiDirIt> * instance, SubMatch match) {
            static_cast<RegexMatcherWithLineInfo<BiDirIt>*>(instance)->flush(instance, match, false);
        };
    }
};

template <typename BiDirIt>
struct RegexSearcher : public RegexMatcher<BiDirIt> {
    using BASE = RegexMatcher<BiDirIt>;
    using SubMatch = typename BASE::SubMatch;
    RegexSearcher() {
        BASE::onMatch = [](RegexMatcher<BiDirIt> * instance, SubMatch match) {
            std::cout << "match: '" << match << "'" << std::endl;
        };
        BASE::onNonMatch = [](RegexMatcher<BiDirIt> * instance, SubMatch match) {
            if (print_non_matches) {
                std::cout << "non match: '" << match << "'" << std::endl;
            }
        };
    }
};

template <typename BiDirIt>
struct RegexSearcherWithLineInfo : public RegexMatcherWithLineInfo<BiDirIt> {
    using BASE = RegexMatcherWithLineInfo<BiDirIt>;
    using SubMatch = typename BASE::SubMatch;
    const char * current_path;
    RegexSearcherWithLineInfo(const char * current_path) : current_path(current_path) {
        BASE::onMatch = [](RegexMatcher<BiDirIt> * instance, SubMatch match) {
            std::cout << "\033[38;2;255;0;0m" << match << "\033[00m";
        };
        BASE::onNonMatch = [](RegexMatcher<BiDirIt> * instance, SubMatch match) {
            std::cout << match;
        };
        BASE::onPrintLine = [](RegexMatcher<BiDirIt> * instance, uint64_t line) {
            const char * color_reset = "\033[00m";
            const char * file_color = "\033[38;2;128;0;255m";
            const char * colon_color = "\033[38;2;255;128;255m";
            const char * line_number_color = "\033[38;2;0;128;255m";
            std::cout << file_color << static_cast<RegexSearcherWithLineInfo<BiDirIt>*>(instance)->current_path << colon_color << ":" << line_number_color << std::to_string(line) << color_reset << ":";
        };
    }
};

void invokeMMAP(const char * path) {
    bool is_searching = search_info.r.size() == 0;

    auto regex_flags = std::regex::ECMAScript | std::regex::optimize;
    if (ignore_case) regex_flags |= std::regex::icase;

    if (is_searching) {

        MMapHelper map(path, 'r');

        if (map.is_open() && map.length() == 0) {
            std::cout << "skipping zero length file: " << path << std::endl;
            return;
        }

        MMapIterator begin(map, 0);
        MMapIterator end(map, map.length());

        std::string search;
        bool first = true;
        for (auto s : search_info.s) {
            if (first) {
                first = false;
            } else {
                search += "|";
            }
            search += unescape(s, true, false);
        }

        if (search.length() == 0) {
            std::cout << "skipping zero length search" << std::endl;
            return;
        }

        unescape(search, true, false);

        std::match_results<MMapIterator> current, prev;

        std::regex e(search, regex_flags);

        std::cout << "searching file '" << path << "' with a length of " << std::to_string(map.length()) << " bytes ..." << std::endl;
        if (print_lines) {
            RegexSearcherWithLineInfo<MMapIterator>(path).search(begin, end, current, prev, e);
        } else {
            RegexSearcher<MMapIterator>().search(begin, end, current, prev, e);
        }
    } else {

        TempFile tmp_file("FindReplace__replace_");
        std::cout << "created temporary file: " << tmp_file.get_path() << std::endl;

        std::size_t old_len;

        {
            MMapHelper map(path, 'r');

            old_len = map.length();

            if (map.is_open() && old_len == 0) {
                std::cout << "skipping zero length file: " << path << std::endl;
                std::cout << "closing temporary file: " << tmp_file.get_path() << std::endl;
                return;
            }

            MMapIterator begin(map, 0);
            MMapIterator end(map, old_len);

            std::string search;
            bool first = true;
            for (auto s : search_info.s) {
                if (first) {
                    first = false;
                } else {
                    search += "|";
                }
                search += unescape(s, true, false);
            }

            if (search.length() == 0) {
                std::cout << "skipping zero length search" << std::endl;
                std::cout << "closing temporary file: " << tmp_file.get_path() << std::endl;
                return;
            }

            unescape(search, true, false);

            std::regex e(search, regex_flags);

            std::match_results<MMapIterator> current, prev;

            std::cout << "searching file '" << path << "' with a length of " << std::to_string(map.length()) << " bytes ..." << std::endl;
            if (print_lines) {
                RegexSearcherWithLineInfo<MMapIterator>(path).search(begin, end, current, prev, e);
            } else {
                RegexSearcher<MMapIterator>().search(begin, end, current, prev, e);
            }

            if (dry_run) {
                std::cout << "replacing (dry run) ..." << std::endl;
            } else {
                std::cout << "replacing ..." << std::endl;
            }
            std::ofstream o (tmp_file.get_path(), std::ios::binary | std::ios::out);

            auto out_iter = std::ostream_iterator<char>(o);
            std::regex_replace(out_iter, begin, end, e, unescape(search_info.r, false, true));

            o.flush();
            o.close();

            // end of mmap scope
        }
        // file is unmapped

        if (dry_run) {
            if (no_detach) {
                std::cout << "closing temporary file: " << tmp_file.get_path() << std::endl;
            } else {
                std::cout << "detaching temporary file: " << tmp_file.get_path() << std::endl;
                tmp_file.detach();
            }
            return;
        }

        MMapHelper map2(tmp_file.get_path().c_str(), 'r');

        if (map2.is_open() && map2.length() == 0) {
            std::cout << "skipping zero length file: " << tmp_file.get_path() << std::endl;
            std::cout << "closing temporary file: " << tmp_file.get_path() << std::endl;
            return;
        }

        auto new_len = map2.length();

        MMapIterator begin_in(map2, 0);
        MMapIterator end_in(map2, new_len);

        std::ofstream o2 (path, std::ios::binary | std::ios::out);
        auto out_iter2 = std::ostream_iterator<char>(o2);
        for (; begin_in != end_in; begin_in++) {
            *out_iter2 = *begin_in;
            out_iter2++;
        }
        o2.flush();
        o2.close();

        if (new_len < old_len) {
            // truncate path to trunc_len
#ifdef _WIN32
            HANDLE handle = CreateFile (
                path,
                GENERIC_WRITE,
                0,
                NULL,
                OPEN_EXISTING,
                NULL,
                NULL
            );

            if (handle == INVALID_HANDLE_VALUE) {
                std::cout << "failed to open truncate file: " << path << std::endl;
                return;
            }

            if (!SetFilePointerEx(handle, (old_len - new_len)+1, NULL, FILE_BEGIN)) {
                std::cout << "failed to set file pointer of truncate file: " << path << std::endl;
                return;
            }

            if (!SetEndOfFile(handle)) {
                std::cout << "failed to truncate file: " << path << std::endl;
                return;
            }

            CloseHandle(handle);
#else
            while (truncate(path, (old_len - new_len)+1) == -1) {
                if (errno == EINTR) continue;
                std::cout << "failed to truncate file: " << path << std::endl;
                return;
            }
#endif
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
        invokeMMAP(path.c_str());
    } else {
        std::cout << "unknown type:  " << path << std::endl;
    }
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
    puts("[1] FindReplace -d/--dir/--dir/-f/--file -s search_items [-r replacement]");
    puts("[2] FindReplace dir/file/--stdin search_item [replacement]");
    puts("");
    puts("--dry-run          no changes will be made during replacement");
    puts("--no-detach        temporary files that are created during a dry run will not be deleted");
    puts("--print-all        print non-matches as well as matches");
    puts("-n                 print file lines as if 'grep -n'");
    puts("-i                 ignore case, '-s abc' can match both 'abc' and 'ABC' and 'aBc'");
    puts("");
    puts("no arguments       this help text");
    puts("-h, --help         this help text");
    puts("");
    puts("");
    puts("[2] mode:");
    puts("dir/file           REQUIRED: the directory/file to search");
    puts("--stdin            REQUIRED: use stdin as file to search");
    puts("  FindReplace --stdin f  #  marks f as the item to search for");
    puts("  FindReplace f --stdin  #  invalid");
    puts("");
    puts("");
    puts("[1] mode:");
    puts("the following can be specified in any order")
    puts("  if any are given, command processing switches from [2] to [1]");
    puts("long option equivilants:");
    puts("  --file");
    puts("  --dir");
    puts("  --directory");
    puts("  --search");
    puts("  --replace");
    puts("-f --stdin         REQUIRED: use stdin as file to search");
    puts("-f FILE            REQUIRED: use FILE as file to search");
    puts("-d DIR             REQUIRED: use DIR as directory to search");
    puts("-s search_items    REQUIRED: the items to search for");
    puts("-r replacement     OPTIONAL: the item to replace with");
    puts("      |");
    puts("      | -r/--replace can be specified multiple times, but only the last one will take effect");
    puts("      | '-r a -r b -r c' will act as if only given '-r c'");
    puts("      |");
    puts("      |  CONSTRAINTS:");
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
    puts("FindReplace -f --stdin -s apple -s a b");
    puts("   searches 'stdin' for the item 'apple', 'a', and 'b'");
    puts("");
    puts("FindReplace -f --stdin -s a foo \"go to space\"");
    puts("   searches 'stdin' for the items 'a', 'foo', and 'go to space'");
    puts("");
    puts("FindReplace -f --stdin -s a \"foo \\n bar\" go -r Alex");
    puts("   searches 'stdin' for the items 'a', 'foo \\n bar', and 'go', and replaces all of these with 'Alex'");
    puts("");
    puts("printf \"fo\\$oba1\\bg2\\\\\\br1\\n2\\n\" > /tmp/foo && FindReplace -- -f /tmp/foo -s \"\\$o\" \"1\\b\" \"2\\\\\\\\\\\\\\b\" \"1\\n2\" -r \"__RACER_X__\"");
    puts("   self explanatory by now, shell $variables are escaped");
    exit(1);
}

std::vector<std::pair<int, std::pair<const char*, bool>>> find_item(int argc, const char** argv, int start_index, std::vector<std::pair<const char*, bool>> items) {
    std::vector<std::pair<int, std::pair<const char*, bool>>> results;
    for (int i = start_index; i < argc; i++) {
        for(std::pair<const char*, bool> & item : items) {
            if (strcmp(argv[i], item.first) == 0 && strlen(argv[i]) == strlen(item.first)) {
                results.push_back({i, item});
            }
        }
    }
    return results;
}

int
#ifdef _WIN32
wmain(int argc, const wchar_t *argv[])
#else
main(int argc, const char*argv[])
#endif
{
    // argc 1 == prog
    // argc 2 == prog arg1
    // argc 3 == prog arg1 arg2
    // argc 4 == prog arg1 arg3 arg3

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--dry-run") == 0) {
            dry_run = true;
        } else if (strcmp(argv[i], "--no-detach") == 0) {
            no_detach = true;
        } else if (strcmp(argv[i], "--print-all") == 0) {
            print_non_matches = true;
        } else if (strcmp(argv[i], "-n") == 0) {
            print_lines = true;
        } else if (strcmp(argv[i], "-i") == 0) {
            ignore_case = true;
        }
    }

    auto items_ = find_item(argc, argv, 1, {{"--dry-run", false}, {"--no-detach", false}, {"--print-all", false}, {"-n", false}, {"-i", false}});
    auto items = find_item(argc, argv, 1, {{"-h", true}, {"--help", true}, {"-f", true}, {"--file", true}, {"-d", true}, {"--dir", true}, {"--directory", true}, {"-s", true}, {"--search", true}, {"-r", true}, {"--replace", true}});
    if (items.size() == 0) {

        if (argc == 1 || argc == 2) {
            help();
        }
        // if we have no flags given, default to DIR, SEARCH, REPLACE
        // we know argc == 3 or more
        // this means   prog arg1 arg2 ...

        search_info.s.push_back(argv[2]);
        if (argc == 4) {
            search_info.r = argv[3];
        }

        auto dir = argv[1];
        if (strcmp(dir, "--stdin") == 0) {

            std::cout << "using stdin as search area" << std::endl;
            std::cout << "searching for:        " << search_info.s[0] << std::endl;
            if (search_info.r.size() != 0) {
                std::cout << "replacing with:       " << search_info.r << std::endl;
            }

            REOPEN_STDIN_AS_BINARY();

            TempFile tmp_file("FindReplace__stdin_");

            std::cout << "created temporary file: " << tmp_file.get_path() << std::endl;

            std::ofstream std__in__file (tmp_file.get_path(), std::ios::binary | std::ios::out);

            toOutStream(&std::cin, &std__in__file, -1, 4096);

            std__in__file.flush();
            std__in__file.close();

            invokeMMAP(tmp_file.get_path().c_str());
        } else {
            std::cout << "directory/file to search:  " << dir << std::endl;
            std::cout << "searching for:        " << search_info.s[0] << std::endl;
            if (search_info.r.size() != 0) {
                std::cout << "replacing with:       " << search_info.r << std::endl;
            }
            lsdir(dir);
        }
    } else {
        // we have flags
        for (auto & extra : items_) items.push_back(extra);
        for (auto & p : items) {
            if (p.second.first != nullptr) {
                if (strcmp(p.second.first, "-h") == 0 || strcmp(p.second.first, "--help") == 0) {
                    help();
                }
            }
        }

        // verify arguments are reasonable
        for (auto & p : items) {
            if (p.second.first != nullptr) {
                if (p.second.second && p.first == argc-1) {
                    std::cout << p.second.first << " must have an argument" << std::endl;
                    exit(1);
                }
                if (p.second.second) {
                    for (auto & p1 : items) {
                        if (p1.second.first != nullptr) {
                            if (p1.first-1 == p.first) {
                                std::cout << p.second.first << " must not be followed by a flag: " << p1.second.first << std::endl;
                                exit(1);
                            }
                        }
                    }
                }
            }
        }

        // collect files
        std::vector<const char *> files;
        std::vector<const char *> directories;
        bool is_stdin = false;
        for (auto & p : items) {
            if (p.second.first != nullptr) {
                if (strcmp(p.second.first, "-f") == 0 || strcmp(p.second.first, "--file") == 0) {
                    if (strcmp(argv[p.first+1], "--stdin") == 0) {
                        is_stdin = true;
                    } else {
                        files.push_back(argv[p.first+1]);
                    }
                } else if (strcmp(p.second.first, "-d") == 0 || strcmp(p.second.first, "--dir") == 0 || strcmp(p.second.first, "--directory") == 0) {
                    directories.push_back(argv[p.first+1]);
                }
            }
        }

        if (!is_stdin && files.size() == 0 && directories.size() == 0) {
            std::cout << "no files/directories specified" << std::endl;
            exit(1);
        }

        // collect search
        std::vector<const char*> searches;
        for (auto & p : items) {
            if (p.second.first != nullptr) {
                if (strcmp(p.second.first, "-s") == 0 || strcmp(p.second.first, "--search") == 0) {
                    // this one is tricky, we can accept multiple items to search, but must stop at a flag
                    bool end_search = false;
                    for (int i = p.first+1; i < argc; i++) {
                        for (auto & p1 : items) {
                            if (p1.second.first != nullptr) {
                                if (p1.first == i) {
                                    end_search = true;
                                    break;
                                }
                            }
                        }
                        if (end_search) break;
                        searches.push_back(argv[i]);
                    }
                }
            }
        }

        if (searches.size() == 0) {
            std::cout << "no search items found" << std::endl;
            exit(1);
        }


        for (auto s : searches) {
            search_info.s.push_back(s);
        }

        // collect replacement
        const char * rep = nullptr;
        for (auto & p : items) {
            if (p.second.first != nullptr) {
                if (strcmp(p.second.first, "-r") == 0 || strcmp(p.second.first, "--replace") == 0) {
                    rep = argv[p.first+1];
                }
            }
        }

        if (rep) {
            search_info.r = rep;
        }

        for (auto f : files) {
            std::cout << "file to search:  " << f << std::endl;
            lsdir(f);
        }
        for (auto d : directories) {
            std::cout << "directory to search:  " << d << std::endl;
            lsdir(d);
        }

        if (is_stdin) {

            std::cout << "using stdin as search area" << std::endl;

            REOPEN_STDIN_AS_BINARY();

            TempFile tmp_file("FindReplace__stdin_");

            std::cout << "created temporary file: " << tmp_file.get_path() << std::endl;

            std::ofstream std__in__file (tmp_file.get_path(), std::ios::binary | std::ios::out);

            toOutStream(&std::cin, &std__in__file, -1, 4096);

            std__in__file.flush();
            std__in__file.close();

            invokeMMAP(tmp_file.get_path().c_str());
        }
    }
    return 0;
}
