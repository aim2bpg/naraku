parser = Naraku::Parser.new(Naraku::Encoding::UTF_8, '\xE3\x81\M-\C-\x62')
node = parser.parse
p node.type
p node.to_h
