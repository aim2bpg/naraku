parser = Naraku::Parser.new(Naraku::Encoding::UTF_8, ".|.+")
node = parser.parse
p node.type
p node.children.length
p node.children[0].type
p node.children[1].type
p node.children[1].child.type
p node.to_h