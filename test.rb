parser = Naraku::Parser.new(Naraku::Encoding::UTF_8, '\\')
node = parser.parse
p node.to_h
