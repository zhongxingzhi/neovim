-- Tests if :mksession saves cursor columns correctly in presence of tab and
-- multibyte characters when fileencoding=utf-8.
--
-- Same as legacy test 93 but using UTF-8 file encoding.

local helpers = require('test.functional.helpers')
local feed, insert, source = helpers.feed, helpers.insert, helpers.source
local clear, execute, expect = helpers.clear, helpers.execute, helpers.expect

describe('store cursor position in session file in UTF-8', function()
  setup(clear)

  teardown(function()
    os.remove('test.in')
    os.remove('test.out')
  end)

  it('is working', function()
    insert([[
      start:
      no multibyte chAracter
      	one leaDing tab
          four leadinG spaces
      two		consecutive tabs
      two	tabs	in one line
      one … multibyteCharacter
      a “b” two multiByte characters
      “c”1€ three mulTibyte characters]])
    -- This test requires the buffer to correspond to a file on disk, here named
    -- "test.in", because otherwise :mksession won't write out the cursor column
    -- info needed for verification.
    execute('write! test.in')

    execute('set sessionoptions=buffers splitbelow fileencoding=utf-8')

    -- Move the cursor through the buffer lines and position it with "|". Using
    -- :split after every normal mode command is a trick to have multiple
    -- cursors on the screen that can all be stored in the session file.
    execute('/^start:')
    execute('vsplit')
    feed('j16|:split<cr>')
    feed('j16|:split<cr>')
    feed('j16|:split<cr>')
    feed('j8|:split<cr>')
    feed('j8|:split<cr>')
    feed('j16|:split<cr>')
    feed('j16|:split<cr>')
    feed('j16|')

    -- Again move the cursor through the buffer and position it with "|". This
    -- time also perform a horizontal scroll at every step.
    execute('wincmd l')
    execute('/^start:')
    execute('set nowrap')
    feed('j16|3zl:split<cr>')
    feed('j016|3zl:split<cr>')
    feed('j016|3zl:split<cr>')
    feed('j08|3zl:split<cr>')
    feed('j08|3zl:split<cr>')
    feed('j016|3zl:split<cr>')
    feed('j016|3zl:split<cr>')
    feed('j016|3zl:split<cr>')

    -- Create the session file, read it back in, and prepare for verification.
    execute('mksession! test.out')
    execute('new test.out')
    execute([[v/\(^ *normal! 0\|^ *exe 'normal!\)/d]])

    -- Assert buffer contents.
    expect([[
      normal! 016|
      normal! 016|
      normal! 016|
      normal! 08|
      normal! 08|
      normal! 016|
      normal! 016|
      normal! 016|
        exe 'normal! ' . s:c . '|zs' . 16 . '|'
        normal! 016|
        exe 'normal! ' . s:c . '|zs' . 16 . '|'
        normal! 016|
        exe 'normal! ' . s:c . '|zs' . 16 . '|'
        normal! 016|
        exe 'normal! ' . s:c . '|zs' . 8 . '|'
        normal! 08|
        exe 'normal! ' . s:c . '|zs' . 8 . '|'
        normal! 08|
        exe 'normal! ' . s:c . '|zs' . 16 . '|'
        normal! 016|
        exe 'normal! ' . s:c . '|zs' . 16 . '|'
        normal! 016|
        exe 'normal! ' . s:c . '|zs' . 16 . '|'
        normal! 016|
        exe 'normal! ' . s:c . '|zs' . 16 . '|'
        normal! 016|]])
  end)
end)
