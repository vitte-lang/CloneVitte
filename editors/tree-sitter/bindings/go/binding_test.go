package tree_sitter_vitte_test

import (
	"testing"

	tree_sitter "github.com/smacker/go-tree-sitter"
	tree_sitter_vitte "github.com/vitte-lang/vitte/editors/tree-sitter/bindings/go"
)

func TestCanLoadGrammar(t *testing.T) {
	language := tree_sitter.NewLanguage(tree_sitter_vitte.Language())
	parser := tree_sitter.NewParser()
	defer parser.Close()
	parser.SetLanguage(language)
	tree := parser.Parse(nil, []byte("proc main() {}"))
	defer tree.Close()
	if tree.RootNode().HasError() {
		t.Fatal("Vitte source failed to parse")
	}
	if language == nil {
		t.Errorf("Error loading Vitte grammar")
	}
}
