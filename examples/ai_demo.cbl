// McBL# v2.0 — AI/Compiler Building Demo
// McBL# cocok untuk bikin AI, compiler, game engine, OS

inc(simple_lexer);

    // Token types sebagai enum
    enum TokenType {
        TOK_NUM,
        TOK_PLUS,
        TOK_MINUS,
        TOK_LPAREN,
        TOK_RPAREN,
        TOK_EOF
    }

    // Token struct
    struct Token {
        type : TokenType
        value : str
    }

    // Lexer function
    func(lex_source);
        #src = inputxt("Enter expression: ")
        $array tokens = {}

        for i range(0, str.len(src)) do;
            #ch = str.char(src, i)
            if ch == "+" do;
                tokens.push({TOK_PLUS, "+"})
            else if ch == "-" do;
                tokens.push({TOK_MINUS, "-"})
            else if ch == "(" do;
                tokens.push({TOK_LPAREN, "("})
            else if ch == ")" do;
                tokens.push({TOK_RPAREN, ")"})
            else if str.contains("0123456789", ch) do;
                tokens.push({TOK_NUM, ch})
            endinc;
        endinc;

        pr("Tokens: " + tokens.len)
        return tokens

    // Jalanin lexer
    #toks = lex_source()
    pr("Done lexing")
    pr(src)

endinc;
