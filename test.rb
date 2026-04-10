parser = Naraku::Parser.new(Naraku::Encoding::UTF_8, '\p{N_e_w_l_i_n_e}')
node = parser.parse
p node.type
p node.to_h
