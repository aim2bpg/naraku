# frozen_string_literal: true

module NarakuRuby
  class Preprocessor
    def call(_context)
      raise NotImplementedError
    end

    protected

    def each_node(node, &)
      yield node
      each_child_node(node) do |child|
        each_node(child, &)
      end
    end

    def each_child_node(node, &)
      case node[:type]
      when :quantifier, :group, :atomic, :absence
        yield node[:child]
      when :assertion
        yield node[:child] if node[:child]
      when :conditional
        yield node[:yes_child]
        yield node[:no_child] if node[:no_child]
      when :concat, :alt
        node[:children].each(&)
      end
    end
  end
end
