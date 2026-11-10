;;; -*- lexical-binding: t; -*-
(require 'ert)
(require 'vitte-mode)
(require 'vitte-indent)

(ert-deftest vitte-indent-nested-code ()
  (with-temp-buffer
    (insert "proc main() {\nif true {\nlet x = \"https://example.com/{\";\n} else {\ncall(\n1,\n2\n);\n}\n}\n")
    (vitte-mode)
    (indent-region (point-min) (point-max))
    (goto-char (point-min))
    (let (actual)
      (while (not (eobp))
        (push (current-indentation) actual)
        (forward-line 1))
      (should (equal (nreverse actual) '(0 4 8 4 8 12 12 8 4 0))))))

(ert-deftest vitte-comment-syntax ()
  (with-temp-buffer
    (vitte-mode)
    (insert "// comment\nlet x = 1;\n/* outer /* nested */ still comment */\nlet y = 2;\n")
    (goto-char (point-min))
    (search-forward "let x")
    (should-not (nth 4 (syntax-ppss)))
    (search-forward "still")
    (should (nth 4 (syntax-ppss)))
    (search-forward "let y")
    (should-not (nth 4 (syntax-ppss)))))

(ert-deftest vitte-build-uses-compile ()
  (cl-letf (((symbol-function 'vitte--file-or-error) (lambda () "/tmp/a b.vit"))
            ((symbol-function 'vitte--run)
             (lambda (command file)
               (should (equal command "compile"))
               (should (equal file "/tmp/a b.vit")))))
    (vitte-build-buffer)))

(ert-run-tests-batch-and-exit)
