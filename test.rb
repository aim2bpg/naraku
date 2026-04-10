parser = Naraku::Parser.new(Naraku::Encoding::UTF_8, '\c\q')
node = parser.parse
p node.type
p node.to_h
