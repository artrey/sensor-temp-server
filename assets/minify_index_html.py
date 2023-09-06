Import("env")

env.Execute("$PYTHONEXE -m pip install minify_html")

def pre_minify_index_html(source, target, env):
    import minify_html

    with open("assets/index.html", "r") as fd:
        html = fd.read()
    minified = minify_html.minify(html, minify_css=True)
    minified = minified.replace('"', '\\"')

    with open("include/index.html.h", "w") as fd:
        fd.write(f"constexpr const char *const INDEX_TEMPLATE = \"{minified}\";")

pre_minify_index_html(None, None, None)
    

# env.AddPreAction("compile", pre_minify_index_html)
