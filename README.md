```
[1] FindReplace -d/--dir/--dir/-f/--file -s search_items [-r replacement]
[2] FindReplace dir/file/--stdin search_item [replacement]

--dry-run          no changes will be made during replacement
--no-detach        temporary files that are created during a dry run will not be deleted
--print-all        print non-matches as well as matches
-n                 print file lines as if 'grep -n'
-i                 ignore case, '-s abc' can match both 'abc' and 'ABC' and 'aBc'

no arguments       this help text
-h, --help         this help text


[2] mode:
dir/file           REQUIRED: the directory/file to search
--stdin            REQUIRED: use stdin as file to search
  FindReplace --stdin f  #  marks f as the item to search for
  FindReplace f --stdin  #  invalid


[1] mode:
the following can be specified in any order, if any are given, command processing switches from [2] to [1]
long option equivilants:
  --file
  --dir
  --directory
  --search
  --replace
-f --stdin         REQUIRED: use stdin as file to search
-f FILE            REQUIRED: use FILE as file to search
-d DIR             REQUIRED: use DIR as directory to search
-s search_items    REQUIRED: the items to search for
-r replacement     OPTIONAL: the item to replace with
      |
      | -r/--replace can be specified multiple times, but only the last one will take effect
      | '-r a -r b -r c' will act as if only given '-r c'
      |
      |  CONSTRAINTS:
      |
      |  -r is required if -s is given
      |
      |   the following are equivilant
      |    \
      |     |- FindReplace ... item_A item_B -r item_C
      |     |   \
      |     |    searches for item_A and item_B and replaces with item_C
      |     |
      |     |- FindReplace ... -s item_A item_B -r item_C
      |         \
      |          searches for item_A and item_B and replaces with item_C
      |
      |
      |   the following are equivilant
      |    \
      |     |- FindReplace ... item_A item_B
      |     |   \
      |     |    searches for item_A and replaces with item_B
      |     |
      |     |- FindReplace ... item_A -r item_B
      |     |   \
      |     |    searches for item_A and replaces with item_B
      |     |
      |     |- FindReplace ... -s item_A -r item_B
      |         \
      |          searches for item_A and replaces with item_B
      |
      |
      |   the following are NOT equivilant
      |    \
      |     |- FindReplace ... -s item_A item_B
      |     |   \
      |     |    searches for item_A and item_B
      |     |
      |     |- FindReplace ... -s item_A -r item_B
      |         \
      |          searches for item_A and replaces with item_B
      |
      |
      |   the following are NOT equivilant
      |    \
      |     |- FindReplace ... -s item_A item_B
      |     |   \
      |     |    searches for item_A and item_B
      |     |
      |     |- FindReplace ... item_A -r item_B
      |         \
      |          searches for item_A and replaces with item_B
      |__________________________________________________________________


EXAMPLES:

FindReplace --stdin "\n" "\r\n"
   searches 'stdin' for the item '\n' (unix) and replaces it with '\r\n' (windows)

FindReplace my_file "\n" "\r\n"
   searches 'my_file' for the item '\n' (unix) and replaces it with '\r\n' (windows)

FindReplace my_dir "\n" "\r\n"
   searches 'my_dir' recursively for the item '\n' (unix) and replaces it with '\r\n' (windows)

FindReplace --stdin apple
   searches 'stdin' for the item 'apple'

FindReplace --stdin "apple pies" "pie kola"
   searches 'stdin' for the item 'apple pies', and replaces it with 'pie kola'

FindReplace -f --stdin -s apple -s a b
   searches 'stdin' for the item 'apple', 'a', and 'b'

FindReplace -f --stdin -s a foo "go to space"
   searches 'stdin' for the items 'a', 'foo', and 'go to space'

FindReplace -f --stdin -s a "foo \n bar" go -r Alex
   searches 'stdin' for the items 'a', 'foo \n bar', and 'go', and replaces all of these with 'Alex'

printf "fo\$oba1\bg2\\\br1\n2\n" > /tmp/foo && FindReplace -- -f /tmp/foo -s "\$o" "1\b" "2\\\\\\\b" "1\n2" -r "__RACER_X__"
   self explanatory by now, shell $variables are escaped
```
