for f in $(find . -name "*.h")
do
 indent "$f" -bbo -nut -npcs -bap -sc -br -ce -cdw -br -brs -nlp -ci4 -l80 -i4 -cli0 -ip0 -T aligned -T __attribute__
done

for f in $(find . -name "*.c")
do
 indent "$f" -bbo -nut -npcs -bap -sc -br -ce -cdw -br -brs -nlp -ci4 -l80 -i4 -cli0 -ip0 -T aligned -T __attribute__
done
