;;; vitte-indent.el --- Advanced indentation engine for Vitte -*- lexical-binding: t; -*-

;; Indent delimiter bodies while ignoring delimiters in strings and comments.

(require 'cl-lib)

(defgroup vitte-indent nil
  "Indentation engine for Vitte."
  :group 'languages)

(defcustom vitte-indent-offset 4
  "Indent width for Vitte."
  :type 'integer
  :group 'vitte-indent)

(defcustom vitte-indent-continuation-offset 4
  "Continuation indentation width."
  :type 'integer
  :group 'vitte-indent)

(defun vitte--blank-line-p ()
  (save-excursion
    (beginning-of-line)
    (looking-at-p "[ \t]*$")))

(defun vitte--compute-indent ()
  "Indent relative to the containing delimiter, ignoring strings and comments."
  (save-excursion
    (back-to-indentation)
    (let* ((state (syntax-ppss))
           (opener (nth 1 state))
           (closing (looking-at-p "[]})]")))
      (cond
       ((or (nth 3 state) (nth 4 state)) (current-indentation))
       (opener
        (goto-char opener)
        (+ (current-indentation)
           (cond (closing 0)
                 ((eq (char-after) ?{) vitte-indent-offset)
                 (t vitte-indent-continuation-offset))))
       (t 0)))))

(defun vitte-indent-line ()
  "Indent current line as Vitte code."
  (interactive)
  (let ((pos (- (point-max) (point)))
        (indent (vitte--compute-indent)))
    (beginning-of-line)
    (delete-horizontal-space)
    (indent-to indent)
    (when (> (- (point-max) pos) (point))
      (goto-char (- (point-max) pos)))))

(defun vitte-indent-region (begin end)
  "Indent Vitte region."
  (interactive "r")
  (let ((limit (copy-marker end)))
    (unwind-protect
        (save-excursion
          (goto-char begin)
          (while (< (point) limit)
            (unless (vitte--blank-line-p)
              (vitte-indent-line))
            (forward-line 1)))
      (set-marker limit nil))))

(defun vitte-newline-and-indent ()
  "Insert newline and indent."
  (interactive)
  (newline)
  (vitte-indent-line))

(defun vitte-electric-brace (arg)
  "Insert brace and reindent."
  (interactive "*P")
  (self-insert-command (prefix-numeric-value arg))
  (when (member last-command-event '(?\} ?\)))
    (save-excursion
      (forward-char -1)
      (vitte-indent-line)))
  (vitte-indent-line))

(defvar vitte-indent-mode-map
  (let ((map (make-sparse-keymap)))

    (define-key map (kbd "RET")
                #'vitte-newline-and-indent)

    (define-key map "}"
                #'vitte-electric-brace)

    map)
  "Keymap for vitte indentation helpers.")

;;;###autoload
(define-minor-mode vitte-indent-mode
  "Advanced indentation support for Vitte."
  :lighter " VIndent"
  :keymap vitte-indent-mode-map

  (if vitte-indent-mode
      (setq-local indent-line-function
                  #'vitte-indent-line)
    (kill-local-variable 'indent-line-function)))

(provide 'vitte-indent)

;;; vitte-indent.el ends here
