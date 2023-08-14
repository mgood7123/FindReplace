#include <cppfs/fs.h>
#include <cppfs/FileHandle.h>
#include <cppfs/FileIterator.h>

#include <sstream>
#include <fstream>

#include <memory>
#include <cstring>

#include <mmap_iterator.h>
#include <ifstream_iterator.h>

#include <tmpfile.h>

#ifdef _WIN32
#include <fileapi.h>
#else
#include <unistd.h>
#include <sys/types.h>
#endif

namespace DarcsPatch {
    // STD IMPL - LLDB by default will not step into std code, this is good EXCEPT if we want to step into DarcsPatch::function
    // stepping into DarcsPatch::function is required in order to step info our assigned function callback

    /**
      *  @brief  Forward an lvalue.
      *  @return The parameter cast to the specified type.
      *
      *  This function is used to implement "perfect forwarding".
      */
    template<typename _Tp>
    constexpr _Tp&&
    forward(typename std::remove_reference<_Tp>::type& __t) noexcept
    { return static_cast<_Tp&&>(__t); }

    /**
      *  @brief  Forward an rvalue.
      *  @return The parameter cast to the specified type.
      *
      *  This function is used to implement "perfect forwarding".
      */
    template<typename _Tp>
    constexpr _Tp&&
    forward(typename std::remove_reference<_Tp>::type&& __t) noexcept
    {
        static_assert(!std::is_lvalue_reference<_Tp>::value, "template argument"
            " substituting _Tp is an lvalue reference type");
        return static_cast<_Tp&&>(__t);
    }

    namespace detail
    {
        template<class>
        constexpr bool is_reference_wrapper_v = false;
        template<class U>
        constexpr bool is_reference_wrapper_v<std::reference_wrapper<U>> = true;
    
        template<class C, class Pointed, class T1, class... Args>
        constexpr decltype(auto) invoke_memptr(Pointed C::* f, T1&& t1, Args&&... args)
        {
            if constexpr (std::is_function_v<Pointed>)
            {
                if constexpr (std::is_base_of_v<C, std::decay_t<T1>>)
                    return (DarcsPatch::forward<T1>(t1).*f)(DarcsPatch::forward<Args>(args)...);
                else if constexpr (is_reference_wrapper_v<std::decay_t<T1>>)
                    return (t1.get().*f)(DarcsPatch::forward<Args>(args)...);
                else
                    return ((*DarcsPatch::forward<T1>(t1)).*f)(DarcsPatch::forward<Args>(args)...);
            }
            else
            {
                static_assert(std::is_object_v<Pointed> && sizeof...(args) == 0);
                if constexpr (std::is_base_of_v<C, std::decay_t<T1>>)
                    return DarcsPatch::forward<T1>(t1).*f;
                else if constexpr (is_reference_wrapper_v<std::decay_t<T1>>)
                    return t1.get().*f;
                else
                    return (*DarcsPatch::forward<T1>(t1)).*f;
            }
        }
    } // namespace detail
    
    template<class F, class... Args>
    constexpr std::invoke_result_t<F, Args...> invoke(F&& f, Args&&... args)
        noexcept(std::is_nothrow_invocable_v<F, Args...>)
    {
        if constexpr (std::is_member_pointer_v<std::decay_t<F>>)
            return DarcsPatch::detail::invoke_memptr(f, DarcsPatch::forward<Args>(args)...);
        else
            return DarcsPatch::forward<F>(f)(DarcsPatch::forward<Args>(args)...);
    }

    template<typename Result,typename ...Args>
    struct abstract_function
    {
        virtual Result operator()(Args... args)=0;
        virtual abstract_function *clone() const =0;
        virtual ~abstract_function() = default;
    };

    template<typename Func,typename Result,typename ...Args>
    class concrete_function: public abstract_function<Result,Args...>
    {
        Func f;
    public:
        concrete_function(const Func &x)
            : f(x)
        {}
        Result operator()(Args... args) override
        {
            return DarcsPatch::invoke(f, args...);
        }
        concrete_function *clone() const override
        {
            return new concrete_function{f};
        }
    };

    template<typename Func>
    struct func_filter
    {
        typedef Func type;
    };
    template<typename Result,typename ...Args>
    struct func_filter<Result(Args...)>
    {
        typedef Result (*type)(Args...);
    };

    template<typename signature>
    class function;

    template<typename Result,typename ...Args>
    class function<Result(Args...)>
    {
        abstract_function<Result,Args...> *f;
    public:
        function()
            : f(nullptr)
        {}
        template<typename Func> function(const Func &x)
            : f(new concrete_function<typename func_filter<Func>::type,Result,Args...>(x))
        {}
        function(const function &rhs)
            : f(rhs.f ? rhs.f->clone() : nullptr)
        {}
        function &operator=(const function &rhs)
        {
            if( (&rhs != this ) && (rhs.f) )
            {
                auto *temp = rhs.f->clone();
                delete f;
                f = temp;
            }
            return *this;
        }
        template<typename Func> function &operator=(const Func &x)
        {
            auto *temp = new concrete_function<typename func_filter<Func>::type,Result,Args...>(x);
            delete f;
            f = temp;
            return *this;
        }
        Result operator()(Args... args) const
        {
            if(f)
                return DarcsPatch::invoke(*f, args...);
            else
                throw std::bad_function_call();
        }
        ~function()
        {
            delete f;
        }
    };
}


#define IFSTREAM_ITERATOR_CHUNK_SIZE (4096*4000)

bool dry_run = false;
bool no_detach = false;
bool print_non_matches = false;
bool print_lines = false;
bool ignore_case = false;
bool silent = false;
bool use_mmap = true;

struct SearchInfo {
    std::vector<std::string> s;
    std::string search;
    std::string r;
} search_info;

#include <regex>

std::string escape(const char c) {
    if (c == '\n') return "\\n";
    else if (c == '\t') return "\\t";
    else if (c == '\r') return "\\r";
    else if (c == '\v') return "\\v";
    else if (c == '\b') return "\\b";
    else if (c == '\\') return "\\\\";
    else {
        char s[2] = {c, '\0'};
        return s;
    }
}

std::string escape(const std::string & s) {
    std::string x;
    if (s.size() != 0) {
        for (int i = 0, m = s.size()-1; i <= m; i++) {
            const char c = s[i];
            if (c == '\n') x.append("\\n");
            else if (c == '\t') x.append("\\t");
            else if (c == '\r') x.append("\\r");
            else if (c == '\v') x.append("\\v");
            else if (c == '\b') x.append("\\b");
            else if (c == '\\') x.append("\\\\");
            else x.push_back(c);
        }
    }
    return x;
}

std::string print_escaped(const std::string& s)
{
    std::string x = escape(s);
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
                if (c == '\\') x.push_back('\\');
                else if (c == 't') x.push_back('\t');
                else if (c == 'r') x.push_back('\r');
                else if (c == 'v') x.push_back('\v');
                else if (c == 'b') x.push_back('\b');
                else {
                    // unknown escape
                    x.push_back('\\');
                    x.push_back(c);
                }
                slash = false;
            } else {
                if ((unescape_regex || unescape_regex_replace) && c == '\\') slash = true;
                else if (unescape_regex && c == '$') x.append("\\$");
                else if (unescape_regex && c == '|') x.append("\\|");
                else if (unescape_regex && c == '^') x.append("\\^");
                else if (unescape_regex && c == '.') x.append("\\.");
                else if (unescape_regex && c == '+') x.append("\\+");
                else if (unescape_regex && c == '-') x.append("\\-");
                else if (unescape_regex && c == '?') x.append("\\?");
                else if (unescape_regex && c == '*') x.append("\\*");
                else if (unescape_regex && c == '(') x.append("\\(");
                else if (unescape_regex && c == ')') x.append("\\)");
                else if (unescape_regex && c == '{') x.append("\\{");
                else if (unescape_regex && c == '}') x.append("\\}");
                else if (unescape_regex && c == '[') x.append("\\[");
                else if (unescape_regex && c == ']') x.append("\\]");
                else if (unescape_regex_replace && c == '$') x.append("$$");
                else x.push_back(c);
            }
        }
        if (slash) {
            x.append("\\\\");
        }
    }
    //print_escaped(x);
  return x;
}

#include <list>

template <typename BiDirIt>
struct RegexMatcher {

    struct SubMatch {
        std::string::iterator s_first, s_second;
        BiDirIt b_first, b_second;
        bool is_bidir = false;

        SubMatch() : is_bidir(false) {}

        SubMatch(std::string::iterator begin, std::string::iterator end) : s_first(begin), s_second(end), is_bidir(false) {}

        SubMatch(BiDirIt begin, BiDirIt end) : b_first(begin), b_second(end), is_bidir(true) {}

        friend std::ostream & operator << (std::ostream & os, const SubMatch & o) {
            if (o.is_bidir) {
                for (BiDirIt begin = o.b_first; begin != o.b_second; begin++) {
                    os << *begin;
                }
            } else {
                for (auto begin = o.s_first; begin != o.s_second; begin++) {
                    os << *begin;
                }
            }
            return os;
        }
    };

    DarcsPatch::function<void(RegexMatcher<BiDirIt> * instance, const SubMatch & match)> onMatch = [](RegexMatcher<BiDirIt> * instance, const SubMatch & match) {}, onNonMatch = [](RegexMatcher<BiDirIt> * instance, const SubMatch & match) {};
    DarcsPatch::function<void(RegexMatcher<BiDirIt> * instance)> onFinish = [](RegexMatcher<BiDirIt> * instance) {};

    bool search(BiDirIt begin, BiDirIt end, std::regex regex) {
        std::match_results<BiDirIt> current, prev;
        return search_ref(begin, end, current, prev, regex);
    }

    bool search(BiDirIt begin, BiDirIt end, std::match_results<BiDirIt> current, std::match_results<BiDirIt> prev, std::regex regex) {
        return search_ref(begin, end, current, prev, regex);
    }

    bool search_ref(BiDirIt & begin, BiDirIt & end, std::match_results<BiDirIt> & current, std::match_results<BiDirIt> & prev, std::regex & regex) {
        bool match = false;
        while(true) {
            // std::cout << std::endl << "start search using std::regex_search" << std::endl;
            bool ret = std::regex_search(begin, end, current, regex);
            // std::cout << "std::regex_search returned " << (ret ? "true" : "false") << std::endl << std::endl;
            if (!ret) break;
            prev = current;
            if (!silent) {
                if (current.size() != 0) {
                    auto n = current.prefix();
                    if (n.first != n.second) {
                        // std::cout << std::endl << "invoking onNonMatch" << std::endl;
                        onNonMatch(this, {n.first, n.second});
                        // std::cout << "invoked onNonMatch" << std::endl << std::endl;
                    }
                }
            }
            for (size_t i = 0; i < current.size(); ++i) {
                auto & n = current[i];
                if (n.first != n.second) {
                    match = true;
                    // std::cout << std::endl << "invoking onMatch" << std::endl;
                    onMatch(this, {n.first, n.second});
                    // std::cout << "invoked onMatch" << std::endl << std::endl;
                }
            }

            auto next_i = current.position() + current.length();
            begin = std::next(begin, next_i);
        }
        if (!silent) {
            if (prev.size() != 0) {
                auto n = prev.suffix();
                if (n.first != n.second) {
                    // std::cout << std::endl << "invoking onNonMatch" << std::endl;
                    onNonMatch(this, {n.first, n.second});
                    // std::cout << "invoked onNonMatch" << std::endl << std::endl;
                }
            } else {
                if (begin != end) {
                    // std::cout << std::endl << "invoking onNonMatch" << std::endl;
                    onNonMatch(this, {begin, end});
                    // std::cout << "invoked onNonMatch" << std::endl << std::endl;
                }
            }
        }
        // std::cout << std::endl << "invoking onFinish" << std::endl;
        onFinish(this);
        // std::cout << "invoked onFinish" << std::endl << std::endl;
        return match;
    }
};

template <typename BiDirIt>
struct RegexMatcherWithLineInfo : public RegexMatcher<BiDirIt> {

    using SubMatch = typename RegexMatcher<BiDirIt>::SubMatch;

    int64_t line = 0;

    DarcsPatch::function<void(RegexMatcher<BiDirIt> * instance, const SubMatch & match)> onMatch = [](RegexMatcher<BiDirIt> * instance, const SubMatch & match) {}, onNonMatch = [](RegexMatcher<BiDirIt> * instance, const SubMatch & match) {};
    DarcsPatch::function<void(RegexMatcher<BiDirIt> * instance, uint64_t line)> onPrintLine = [](RegexMatcher<BiDirIt> * instance, uint64_t line) {};

    private:

    std::list<std::string> accumulation;
    bool was_on_match = false;
    bool needs_reset = true;
    bool line_has_match = false;

    using MATCHES = std::vector<std::pair<DarcsPatch::function<void(RegexMatcher<BiDirIt> * instance, const SubMatch & match)>, SubMatch>>;

    MATCHES matches;

    void reset(RegexMatcher<BiDirIt> * instance, DarcsPatch::function<void(RegexMatcher<BiDirIt> * instance, const SubMatch & match)> & match_func) {
        if (print_lines && (print_non_matches || line_has_match)) {
            if (matches.size() != 0) {
                onPrintLine(instance, line);
                for (auto & p : matches) {
                    p.first(instance, p.second);
                }
                if (accumulation.back().size() != 0) {
                    match_func(instance, {accumulation.back().begin(), accumulation.back().end()});
                }
            } else {
                if (accumulation.back().size() != 0) {
                    onPrintLine(instance, line);
                    match_func(instance, {accumulation.back().begin(), accumulation.back().end()});
                }
            }
        }
        line++;
        if (!print_lines && (print_non_matches || line_has_match)) {
            if (accumulation.back().size() != 0) {
                onPrintLine(instance, line);
                match_func(instance, {accumulation.back().begin(), accumulation.back().end()});
            }
        }
        accumulation = std::move(std::list<std::string>());
        accumulation.emplace_back(std::string());
        matches = std::move(MATCHES());
    }

    void finish(RegexMatcher<BiDirIt> * instance, DarcsPatch::function<void(RegexMatcher<BiDirIt> * instance, const SubMatch & match)> & match_func, DarcsPatch::function<void(RegexMatcher<BiDirIt> * instance, const SubMatch & match)> & match_func_opposite, bool from_on_match) {
        if (was_on_match != from_on_match && accumulation.back().size() != 0) {
            if (print_lines) {
                matches.push_back({match_func_opposite, {accumulation.back().begin(), accumulation.back().end()}});
            } else {
                match_func_opposite(instance, {accumulation.back().begin(), accumulation.back().end()});
            }
            accumulation.emplace_back(std::string());
            was_on_match = from_on_match;
        }
        if (needs_reset) {
            reset(instance, match_func);
            needs_reset = false;
            line_has_match = false;
            std::cout.flush();
        }
    }
    void process(RegexMatcher<BiDirIt> * instance, DarcsPatch::function<void(RegexMatcher<BiDirIt> * instance, const SubMatch & match)> & match_func, DarcsPatch::function<void(RegexMatcher<BiDirIt> * instance, const SubMatch & match)> & match_func_opposite, bool from_on_match, const char c) {
        // std::cout << "process: char = " << escape(c) << std::endl;
        finish(this, match_func, match_func_opposite, from_on_match);
        accumulation.back().push_back(c);
        if (c == '\n') {
            needs_reset = true;
        }
        if (from_on_match) line_has_match = true;
    }
    void flush(RegexMatcher<BiDirIt> * instance, const SubMatch & match, bool from_on_match) {
        auto match_func = from_on_match ? onMatch : onNonMatch;
        auto match_func_opposite = !from_on_match ? onMatch : onNonMatch;
        if (match.is_bidir) {
            for (BiDirIt begin = match.b_first; begin != match.b_second; begin++) {
                process(instance, match_func, match_func_opposite, from_on_match, *begin);
            }
        } else {
            for (auto begin = match.s_first; begin != match.s_second; begin++) {
                process(instance, match_func, match_func_opposite, from_on_match, *begin);
            }
        }
        was_on_match = from_on_match;
    }

    public:

    RegexMatcherWithLineInfo() {
        RegexMatcher<BiDirIt>::onMatch = [](RegexMatcher<BiDirIt> * instance, const SubMatch & match) {
            static_cast<RegexMatcherWithLineInfo<BiDirIt>*>(instance)->flush(instance, match, true);
        };
        RegexMatcher<BiDirIt>::onNonMatch = [](RegexMatcher<BiDirIt> * instance, const SubMatch & match) {
            static_cast<RegexMatcherWithLineInfo<BiDirIt>*>(instance)->flush(instance, match, false);
        };
        RegexMatcher<BiDirIt>::onFinish = [](RegexMatcher<BiDirIt> * instance) {
            if (static_cast<RegexMatcherWithLineInfo<BiDirIt>*>(instance)->needs_reset) {
                static_cast<RegexMatcherWithLineInfo<BiDirIt>*>(instance)->finish(instance, static_cast<RegexMatcherWithLineInfo<BiDirIt>*>(instance)->onNonMatch, static_cast<RegexMatcherWithLineInfo<BiDirIt>*>(instance)->onMatch, false);
            } else {
                static_cast<RegexMatcherWithLineInfo<BiDirIt>*>(instance)->needs_reset = true;
                static_cast<RegexMatcherWithLineInfo<BiDirIt>*>(instance)->accumulation.back().push_back('\n');
                static_cast<RegexMatcherWithLineInfo<BiDirIt>*>(instance)->finish(instance, static_cast<RegexMatcherWithLineInfo<BiDirIt>*>(instance)->onNonMatch, static_cast<RegexMatcherWithLineInfo<BiDirIt>*>(instance)->onMatch, false);
            }
        };
        accumulation.emplace_back(std::string());
    }
};

template <typename BiDirIt>
struct RegexSearcher : public RegexMatcher<BiDirIt> {
    using BASE = RegexMatcher<BiDirIt>;
    using SubMatch = typename BASE::SubMatch;
    RegexSearcher() {
        BASE::onMatch = [](RegexMatcher<BiDirIt> * instance, const SubMatch & match) {
            if (!silent) {
                std::cout << "match: '" << match << "'" << std::endl;
            }
        };
        BASE::onNonMatch = [](RegexMatcher<BiDirIt> * instance, const SubMatch & match) {
            if (!silent) {
                if (print_non_matches) {
                    std::cout << "non match: '" << match << "'" << std::endl;
                }
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
        BASE::onMatch = [](RegexMatcher<BiDirIt> * instance, const SubMatch & match) {
            if (!silent) {
                std::cout << "\033[38;2;255;0;0m" << match << "\033[00m";
            }
        };
        BASE::onNonMatch = [](RegexMatcher<BiDirIt> * instance, const SubMatch & match) {
            if (!silent) {
                std::cout << match;
            }
        };
        BASE::onPrintLine = [](RegexMatcher<BiDirIt> * instance, uint64_t line) {
            if (!silent) {
                const char * color_reset = "\033[00m";
                const char * file_color = "\033[38;2;128;0;255m";
                const char * colon_color = "\033[38;2;255;128;255m";
                const char * line_number_color = "\033[38;2;0;128;255m";
                std::cout << file_color << static_cast<RegexSearcherWithLineInfo<BiDirIt>*>(instance)->current_path << colon_color << ":" << line_number_color << std::to_string(line) << color_reset << ":";
            }
        };
    }
};

bool invokeMMAP(const char * path) {
    bool is_searching = search_info.r.size() == 0;

    auto regex_flags = std::regex::ECMAScript | std::regex::optimize;
    if (ignore_case) regex_flags |= std::regex::icase;

    if (is_searching) {

        if (use_mmap) {
            MMapHelper map(path, 'r');

            auto map_len = map.length();

            if (map.is_open() && map_len == 0) {
                std::cout << "skipping zero length file: " << path << std::endl;
                return false;
            }

            MMapIterator begin(map, 0);
            MMapIterator end(map, map_len);

            std::regex e(search_info.search, regex_flags);

            std::cout << "searching file '" << path << "' with a length of " << std::to_string(map_len) << " bytes ..." << std::endl;
            std::cout << "using mmap api" << std::endl;
            // for (auto begin_ = begin; begin_ != end; begin_++) {
            //     auto c = *begin_;
            // }
            // return true;
            if (print_lines && !silent) {
                return RegexSearcherWithLineInfo<MMapIterator>(path).search(begin, end, e);
            } else {
                return RegexSearcher<MMapIterator>().search(begin, end, e);
            }
        } else {
            std::regex e(search_info.search, regex_flags);

            std::cout << "searching file '" << path << "' ..." << std::endl;
            std::cout << "using ifstream api" << std::endl;
            auto stream = std::ifstream(path, std::ios::binary | std::ios::in);
            // for (std::string line; std::getline(stream, line); ) {

            // }
            // return true;
            auto begin = ifstream_iterator(stream, 0);
            auto end = ifstream_iterator(stream);
            // for (auto begin_ = begin; begin_ != end; begin_++) {
            //     auto c = *begin_;
            // }
            // return true;
            if (print_lines && !silent) {
                return RegexSearcherWithLineInfo<ifstream_iterator>(path).search(begin, end, e);
            } else {
                return RegexSearcher<ifstream_iterator>().search(begin, end, e);
            }
        }
    } else {

        TempFile tmp_file("FindReplace__replace_", true);

        std::size_t old_len;

        if (use_mmap) {
            MMapHelper map(path, 'r');

            old_len = map.length();

            if (map.is_open() && old_len == 0) {
                std::cout << "skipping zero length file: " << path << std::endl;
                return false;
            }

            MMapIterator begin(map, 0);
            MMapIterator end(map, old_len);

            std::regex e(search_info.search, regex_flags);

            std::cout << "searching file '" << path << "' with a length of " << std::to_string(map.length()) << " bytes ..." << std::endl;
            std::cout << "using mmap api" << std::endl;
            if (print_lines && !silent) {
                if (!RegexSearcherWithLineInfo<MMapIterator>(path).search(begin, end, e)) {
                    return false;
                }
            } else {
                if (!RegexSearcher<MMapIterator>().search(begin, end, e)) {
                    return false;
                }
            }

            if (dry_run) {
                std::cout << "replacing (dry run) ..." << std::endl;
            } else {
                std::cout << "replacing ..." << std::endl;
            }
            std::ofstream o (tmp_file.get_path(), std::ios::binary | std::ios::out);

            auto out_iter = std::ostream_iterator<char>(o);

            std::regex_replace(out_iter, begin, end, e, search_info.r);

            o.flush();
            o.close();

            // end of mmap scope
        } else {
            std::regex e(search_info.search, regex_flags);

            std::cout << "searching file '" << path << "' ..." << std::endl;
            std::cout << "using ifstream api" << std::endl;
            auto stream = std::ifstream(path, std::ios::binary | std::ios::in);

            ifstream_iterator::State stream_init;
            stream_init.save(&stream);

            auto begin = ifstream_iterator(stream, 0);
            auto end = ifstream_iterator(stream);

            if (print_lines && !silent) {
                if (!RegexSearcherWithLineInfo<ifstream_iterator>(path).search(begin, end, e)) {
                    return false;
                }
            } else {
                if (!RegexSearcher<ifstream_iterator>().search(begin, end, e)) {
                    return false;
                }
            }

            if (dry_run) {
                std::cout << "replacing (dry run) ..." << std::endl;
            } else {
                std::cout << "replacing ..." << std::endl;
            }

            std::ofstream o (tmp_file.get_path(), std::ios::binary | std::ios::out);

            auto out_iter = std::ostream_iterator<char>(o);

            stream_init.restore(&stream);
            auto begin_ = ifstream_iterator(stream, 0);
            auto end_ = ifstream_iterator(stream);
            std::regex_replace(out_iter, begin_, end_, e, search_info.r);

            o.flush();
            o.close();

            // end of mmap scope
        }

        if (dry_run) {
            // for sake of readability
            if (no_detach) {
                return false;
            } else {
                tmp_file.detach();
                return false;
            }
        }

        MMapHelper map2(tmp_file.get_path().c_str(), 'r');

        auto new_len = map2.length();

        if (map2.is_open() && new_len == 0) {
            std::cout << "skipping zero length file: " << tmp_file.get_path() << std::endl;
            return false;
        }


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
                return false;
            }

            if (!SetFilePointerEx(handle, (old_len - new_len)+1, NULL, FILE_BEGIN)) {
                std::cout << "failed to set file pointer of truncate file: " << path << std::endl;
                return false;
            }

            if (!SetEndOfFile(handle)) {
                std::cout << "failed to truncate file: " << path << std::endl;
                return false;
            }

            CloseHandle(handle);
#else
            while (truncate(path, (old_len - new_len)+1) == -1) {
                if (errno == EINTR) continue;
                std::cout << "failed to truncate file: " << path << std::endl;
                return false;
            }
#endif
        }
        return true;
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

bool lsdir(const std::string & path)
{
    bool m = false;
    cppfs::FileHandle handle = cppfs::fs::open(path);

    if (!handle.exists()) {
        std::cout << "item does not exist:  " << path << std::endl;
        return false;
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
        bool r = invokeMMAP(path.c_str());
        if (!m) m = r;
    } else {
        std::cout << "unknown type:  " << path << std::endl;
    }
    return m;
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
    puts("--silent           dont print any matches from search");
    puts("-n                 print file lines as if 'grep -n'");
    puts("-i                 ignore case, '-s abc' can match both 'abc' and 'ABC' and 'aBc'");
    puts("--no-mmap          uses std::ifstream + ifstream_iterator (SLOW) instead of the mmap (windows/unix) api wrapper");
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
    puts("the following can be specified in any order");
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
        } else if (strcmp(argv[i], "--silent") == 0) {
            silent = true;
        } else if (strcmp(argv[i], "--no-mmap") == 0) {
            use_mmap = false;
        }
    }

    auto items_ = find_item(argc, argv, 1, {{"--dry-run", false}, {"--no-detach", false}, {"--print-all", false}, {"-n", false}, {"-i", false}, {"--silent", false}, {"--no-mmap", false}});
    auto items = find_item(argc, argv, 1, {{"-h", true}, {"--help", true}, {"-f", true}, {"--file", true}, {"-d", true}, {"--dir", true}, {"--directory", true}, {"-s", true}, {"--search", true}, {"-r", true}, {"--replace", true}});
    if (items.size() == 0) {

        if (argc == 1 || argc == 2) {
            help();
        }
        // if we have no flags given, default to DIR, SEARCH, REPLACE
        // we know argc == 3 or more
        // this means   prog arg1 arg2 ...

        {
            search_info.search = unescape(argv[2], true, false);

            if (search_info.search.length() == 0) {
                std::cout << "skipping zero length search" << std::endl;
                return 1;
            }
        }
        if (argc == 4) {
            search_info.r = unescape(argv[3], false, true);
        }
        auto dir = argv[1];
        if (strcmp(dir, "--stdin") == 0) {

            std::cout << "using stdin as search area" << std::endl;
            std::cout << "searching for:        " << escape(search_info.search) << std::endl;
            if (search_info.r.size() != 0) {
                std::cout << "replacing with:       " << escape(search_info.r) << std::endl;
            }

            REOPEN_STDIN_AS_BINARY();

            TempFile tmp_file("FindReplace__stdin_");

            std::cout << "created temporary file: " << tmp_file.get_path() << std::endl;

            std::ofstream std__in__file (tmp_file.get_path(), std::ios::binary | std::ios::out);

            toOutStream(&std::cin, &std__in__file, -1, 4096);

            std__in__file.flush();
            std__in__file.close();

            return invokeMMAP(tmp_file.get_path().c_str()) ? 0 : 1;
        } else {
            std::cout << "directory/file to search:  " << dir << std::endl;
            std::cout << "searching for:        " << escape(search_info.search) << std::endl;
            if (search_info.r.size() != 0) {
                std::cout << "replacing with:       " << escape(search_info.r) << std::endl;
            }
            return lsdir(dir) ? 0 : 1;
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
                    return 1;
                }
                if (p.second.second) {
                    for (auto & p1 : items) {
                        if (p1.second.first != nullptr) {
                            if (p1.first-1 == p.first) {
                                std::cout << p.second.first << " must not be followed by a flag: " << p1.second.first << std::endl;
                                return 1;
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
            return 1;
        }

        // collect search
        {
            bool first = true;
            for (auto & p : items) {
                if (p.second.first != nullptr) {
                    if (strcmp(p.second.first, "-s") == 0 || strcmp(p.second.first, "--search") == 0) {
                        // this one is a bit tricky, we can accept multiple items to search, but must stop at a flag
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
                            if (first) {
                                first = false;
                            } else {
                                search_info.search += "|";
                            }
                            search_info.search += unescape(argv[i], true, false);
                        }
                    }
                }
            }

            if (search_info.search.length() == 0) {
                std::cout << "skipping zero length search" << std::endl;
                return 1;
            }
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
            search_info.r = unescape(rep, false, true);
        }

        bool m = false;

        std::cout << "searching for:        " << escape(search_info.search) << std::endl;
        if (search_info.r.size() != 0) {
            std::cout << "replacing with:       " << escape(search_info.r) << std::endl;
        }

        for (auto f : files) {
            std::cout << "file to search:  " << f << std::endl;
            bool r = lsdir(f);
            if (!m) m = r;
        }
        for (auto d : directories) {
            std::cout << "directory to search:  " << d << std::endl;
            bool r = lsdir(d);
            if (!m) m = r;
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

            bool r = invokeMMAP(tmp_file.get_path().c_str());
            if (!m) m = r;
        }
        return m ? 0 : 1;
    }
}
