# -*- coding: utf-8 -*-
#
# python-gammu documentation build configuration file, created by
# sphinx-quickstart on Tue Mar 10 18:14:17 2009.
#
# This file is execfile()d with the current directory set to its
# containing dir.
#
# Note that not all possible configuration values are present in this
# autogenerated file.
#
# All configuration values have a default; values that are commented out
# serve to show the default.

import sys
import os
import shlex

# Change what .. option:: parses
import sphinx.domains.std
import re

try:
    import alabaster
    has_alabaster = True
except ImportError:
    has_alabaster = False

on_rtd = os.environ.get('READTHEDOCS', None) == 'True'

def gammu_process_link(self, env, refnode, has_explicit_title, title, target):
    program = env.temp_data.get('std:program')
    if not has_explicit_title:
        if ' ' in title and not (title.startswith('/') or
                                 title.startswith('-')):
            program, target = re.split(' (?=-|--|/)?', title, 1)
            program = sphinx.domains.std.ws_re.sub('-', program)
            target = target.strip()
    elif ' ' in target:
        program, target = re.split(' (?=-|--|/)?', target, 1)
        program = sphinx.domains.std.ws_re.sub('-', program)
    refnode['refprogram'] = program
    return title, target

sphinx.domains.std.option_desc_re = re.compile(
    r'((?:/|-|--|^)[-_a-zA-Z0-9]+)(\s*.*?)(?=,\s+(?:/|-|--)|$)')
sphinx.domains.std.OptionXRefRole.process_link = gammu_process_link

# If extensions (or modules to document with autodoc) are in another directory,
# add these directories to sys.path here. If the directory is relative to the
# documentation root, use os.path.abspath to make it absolute, like shown here.
sys.path.append('@CMAKE_CURRENT_SOURCE_DIR@')
sys.path.append(os.path.abspath(os.path.dirname(__file__)))

# -- General configuration ------------------------------------------------

# If your documentation needs a minimal Sphinx version, state it here.
#needs_sphinx = '1.0'

# Add any Sphinx extension module names here, as strings. They can be
# extensions coming with Sphinx (named 'sphinx.ext.*') or your custom
# ones.
extensions = ['breathe', 'configext', 'sphinx.ext.graphviz', 'sphinx.ext.intersphinx']

intersphinx_mapping = {'python': ('https://docs.python.org/3.4', None)}

# Add any paths that contain templates here, relative to this directory.
templates_path = ['@CMAKE_CURRENT_SOURCE_DIR@/.templates']

# The suffix(es) of source filenames.
# You can specify multiple suffix as a list of string:
# source_suffix = ['.rst', '.md']
source_suffix = '.rst'

# The encoding of source files.
#source_encoding = 'utf-8-sig'

# The master toctree document.
master_doc = 'index'

# General information about the project.
project = u'Gammu'
copyright = u'2009-2015, Michal Čihař <michal@cihar.com>'
author = u'Michal Čihař <michal@cihar.com>'

# The version info for the project you're documenting, acts as replacement for
# |version| and |release|, also used in various other places throughout the
# built documents.
#
# The short X.Y version.
version = '@GAMMU_VERSION@'
# The full version, including alpha/beta/rc tags.
release = version

# The language for content autogenerated by Sphinx. Refer to documentation
# for a list of supported languages.
#
# This is also used if you do content translation via gettext catalogs.
# Usually you set "language" from the command line for these cases.
language = None

# There are two options for replacing |today|: either, you set today to some
# non-false value, then it is used:
#today = ''
# Else, today_fmt is used as the format for a strftime call.
#today_fmt = '%B %d, %Y'

# List of patterns, relative to source directory, that match files and
# directories to ignore when looking for source files.
exclude_patterns = ['.build']

# The reST default role (used for this markup: `text`) to use for all
# documents.
#default_role = None

# If true, '()' will be appended to :func: etc. cross-reference text.
#add_function_parentheses = True

# If true, the current module name will be prepended to all description
# unit titles (such as .. function::).
#add_module_names = True

# If true, sectionauthor and moduleauthor directives will be shown in the
# output. They are ignored by default.
#show_authors = False

# The name of the Pygments (syntax highlighting) style to use.
pygments_style = 'sphinx'

# A list of ignored prefixes for module index sorting.
#modindex_common_prefix = []

# If true, keep warnings as "system message" paragraphs in the built documents.
#keep_warnings = False

# If true, `todo` and `todoList` produce output, else they produce nothing.
todo_include_todos = False


# -- Options for HTML output ----------------------------------------------

# The theme to use for HTML and HTML Help pages.  See the documentation for
# a list of builtin themes.
if on_rtd or not has_alabaster:
    html_theme = 'default'
else:
    html_theme = 'alabaster'

# Theme options are theme-specific and customize the look and feel of a theme
# further.  For a list of options available for each theme, see the
# documentation.
#html_theme_options = {}

# Add any paths that contain custom themes here, relative to this directory.
html_theme_path = ['.']

# The name for this set of Sphinx documents.  If None, it defaults to
# "<project> v<release> documentation".
#html_title = None

# A shorter title for the navigation bar.  Default is the same as html_title.
#html_short_title = None

# The name of an image file (relative to this directory) to place at the top
# of the sidebar.
#html_logo = None

# The name of an image file (within the static path) to use as favicon of the
# docs.  This file should be a Windows icon file (.ico) being 16x16 or 32x32
# pixels large.
#html_favicon = None

# Add any paths that contain custom static files (such as style sheets) here,
# relative to this directory. They are copied after the builtin static files,
# so a file named "default.css" will overwrite the builtin "default.css".
html_static_path = []

# Add any extra paths that contain custom files (such as robots.txt or
# .htaccess) here, relative to this directory. These files are copied
# directly to the root of the documentation.
#html_extra_path = []

# If not '', a 'Last updated on:' timestamp is inserted at every page bottom,
# using the given strftime format.
#html_last_updated_fmt = '%b %d, %Y'

# If true, SmartyPants will be used to convert quotes and dashes to
# typographically correct entities.
#html_use_smartypants = True

# Custom sidebar templates, maps document names to template names.
#html_sidebars = {}

# Additional templates that should be rendered to pages, maps page names to
# template names.
#html_additional_pages = {}

# If false, no module index is generated.
#html_domain_indices = True

# If false, no index is generated.
#html_use_index = True

# If true, the index is split into individual pages for each letter.
#html_split_index = False

# If true, links to the reST sources are added to the pages.
#html_show_sourcelink = True

# If true, "Created using Sphinx" is shown in the HTML footer. Default is True.
#html_show_sphinx = True

# If true, "(C) Copyright ..." is shown in the HTML footer. Default is True.
#html_show_copyright = True

# If true, an OpenSearch description file will be output, and all pages will
# contain a <link> tag referring to it.  The value of this option must be the
# base URL from which the finished HTML is served.
#html_use_opensearch = ''

# This is the file name suffix for HTML files (e.g. ".xhtml").
#html_file_suffix = None

# Language to be used for generating the HTML full-text search index.
# Sphinx supports the following languages:
#   'da', 'de', 'en', 'es', 'fi', 'fr', 'hu', 'it', 'ja'
#   'nl', 'no', 'pt', 'ro', 'ru', 'sv', 'tr'
#html_search_language = 'en'

# A dictionary with options for the search language support, empty by default.
# Now only 'ja' uses this config value
#html_search_options = {'type': 'default'}

# The name of a javascript file (relative to the configuration directory) that
# implements a search results scorer. If empty, the default will be used.
#html_search_scorer = 'scorer.js'

# Output file base name for HTML help builder.
htmlhelp_basename = 'gammudoc'

# -- Options for LaTeX output ---------------------------------------------

latex_elements = {
# The paper size ('letterpaper' or 'a4paper').
    'papersize': 'a4',

# The font size ('10pt', '11pt' or '12pt').
#'pointsize': '10pt',

# Additional stuff for the LaTeX preamble.
#'preamble': '',

# Latex figure (float) alignment
#'figure_align': 'htbp',
}

# Grouping the document tree into LaTeX files. List of tuples
# (source start file, target name, title,
#  author, documentclass [howto, manual, or own class]).
latex_documents = [
  ('contents', 'gammu.tex', ur'Gammu Manual',
   ur'Michal Čihař <michal@cihar.com>', 'manual', True),
  ('smsd/index', 'smsd.tex', ur'Gammu SMSD Daemon Manual',
   ur'Michal Čihař <michal@cihar.com>', 'manual', True),
]

# The name of an image file (relative to this directory) to place at the top of
# the title page.
#latex_logo = None

# For "manual" documents, if this is true, then toplevel headings are parts,
# not chapters.
#latex_use_parts = False

# If true, show page references after internal links.
#latex_show_pagerefs = False

# If true, show URL addresses after external links.
#latex_show_urls = False

# Documents to append as an appendix to all manuals.
#latex_appendices = []

# If false, no module index is generated.
latex_domain_indices = True

# -- Options for breathe extension ----------------------------------------

breathe_projects = {
    'api':'@DOXYGEN_OUTPUT@/xml',
}

breathe_default_project = 'api'


breathe_domain_by_extension = {
    "h" : "c",
}

# -- Options for manual page output ---------------------------------------

# One entry per manual page. List of tuples
# (source start file, name, description, authors, manual section).
man_pages = [
    ('smsd/files', 'gammu-smsd-files', 'gammu-smsd(1) backend using filesystem as a message storage', u'Michal Čihař <michal@cihar.com>', 7),
    ('smsd/tables', 'gammu-smsd-tables', 'description of tables for database backends of gammu-smsd(1)', u'Michal Čihař <michal@cihar.com>', 7),
    ('smsd/mysql', 'gammu-smsd-mysql', 'gammu-smsd(1) backend using MySQL database server as a message storage', u'Michal Čihař <michal@cihar.com>', 7),
    ('smsd/pgsql', 'gammu-smsd-pgsql', 'gammu-smsd(1) backend using PostgreSQL database server as a message storage', u'Michal Čihař <michal@cihar.com>', 7),
    ('smsd/dbi', 'gammu-smsd-dbi', 'gammu-smsd(1) backend using DBI abstraction layer to use any supported database as a message storage', u'Michal Čihař <michal@cihar.com>', 7),
    ('smsd/odbc', 'gammu-smsd-odbc', 'gammu-smsd(1) backend using ODBC abstraction layer to use any supported database as a message storage', u'Michal Čihař <michal@cihar.com>', 7),
    ('smsd/sql', 'gammu-smsd-sql', 'gammu-smsd(1) backend using SQL abstraction layer to use any supported database as a message storage', u'Michal Čihař <michal@cihar.com>', 7),
    ('smsd/run', 'gammu-smsd-run', 'documentation for RunOnReceive directive', u'Michal Čihař <michal@cihar.com>', 7),
    ('smsd/null', 'gammu-smsd-null', 'gammu-smsd(1) backend not storing messages', u'Michal Čihař <michal@cihar.com>', 7),
    ('smsd/config', 'gammu-smsdrc', 'gammu-smsd(1) configuration file', u'Michal Čihař <michal@cihar.com>', 5),
    ('smsd/inject', 'gammu-smsd-inject', 'Inject messages into queue of SMS daemon for Gammu', u'Michal Čihař <michal@cihar.com>', 1),
    ('smsd/monitor', 'gammu-smsd-monitor', 'Monitor state of SMS daemon for Gammu', u'Michal Čihař <michal@cihar.com>', 1),
    ('smsd/smsd', 'gammu-smsd', 'SMS daemon for Gammu', u'Michal Čihař <michal@cihar.com>', 1),
    ('config/index', 'gammurc', 'gammu(1) configuration file', u'Michal Čihař <michal@cihar.com>', 5),
    ('gammu/index', 'gammu', 'Does some neat things with your cellular phone or modem.', u'Michal Čihař <michal@cihar.com>', 1),
    ('formats/backup', 'gammu-backup', 'gammu(1) backup file format.', u'Michal Čihař <michal@cihar.com>', 5),
    ('formats/smsbackup', 'gammu-smsbackup', 'gammu(1) SMS backup file format.', u'Michal Čihař <michal@cihar.com>', 5),
    ('utils/jadmaker', 'jadmaker', 'JAD File Generator', u'Michal Čihař <michal@cihar.com>', 1),
    ('utils/gammu-config', 'gammu-config', 'Gammu configurator', u'Michal Čihař <michal@cihar.com>', 1),
    ('utils/gammu-detect', 'gammu-detect', 'Gammu device detection', u'Michal Čihař <michal@cihar.com>', 1),
]

# If true, show URL addresses after external links.
#man_show_urls = False


# -- Options for Texinfo output -------------------------------------------

# Grouping the document tree into Texinfo files. List of tuples
# (source start file, target name, title, author,
#  dir menu entry, description, category)
texinfo_documents = [
  (master_doc, 'testproject', u'testproject Documentation',
   author, 'testproject', 'One line description of project.',
   'Miscellaneous'),
]

# Documents to append as an appendix to all manuals.
#texinfo_appendices = []

# If false, no module index is generated.
#texinfo_domain_indices = True

# How to display URL addresses: 'footnote', 'no', or 'inline'.
#texinfo_show_urls = 'footnote'

# If true, do not generate a @detailmenu in the "Top" node's menu.
#texinfo_no_detailmenu = False


# -- Options for Epub output ----------------------------------------------

# Bibliographic Dublin Core info.
epub_title = project
epub_author = author
epub_publisher = author
epub_copyright = copyright

# The basename for the epub file. It defaults to the project name.
#epub_basename = project

# The HTML theme for the epub output. Since the default themes are not optimized
# for small screen space, using the same theme for HTML and epub output is
# usually not wise. This defaults to 'epub', a theme designed to save visual
# space.
#epub_theme = 'epub'

# The language of the text. It defaults to the language option
# or 'en' if the language is not set.
#epub_language = ''

# The scheme of the identifier. Typical schemes are ISBN or URL.
#epub_scheme = ''

# The unique identifier of the text. This can be a ISBN number
# or the project homepage.
#epub_identifier = ''

# A unique identification for the text.
#epub_uid = ''

# A tuple containing the cover image and cover page html template filenames.
#epub_cover = ()

# A sequence of (type, uri, title) tuples for the guide element of content.opf.
#epub_guide = ()

# HTML files that should be inserted before the pages created by sphinx.
# The format is a list of tuples containing the path and title.
#epub_pre_files = []

# HTML files shat should be inserted after the pages created by sphinx.
# The format is a list of tuples containing the path and title.
#epub_post_files = []

# A list of files that should not be packed into the epub file.
epub_exclude_files = ['search.html']

# The depth of the table of contents in toc.ncx.
#epub_tocdepth = 3

# Allow duplicate toc entries.
#epub_tocdup = True

# Choose between 'default' and 'includehidden'.
#epub_tocscope = 'default'

# Fix unsupported image types using the Pillow.
#epub_fix_images = False

# Scale large images.
#epub_max_image_width = 0

# How to display URL addresses: 'footnote', 'no', or 'inline'.
#epub_show_urls = 'inline'

# If false, no index is generated.
#epub_use_index = True

graphviz_output_format = 'svg'
