inc(2D Game Engine Demo);
  CARGO_m(4194304)
  AUTOCLEAR_cargo

  dev abs_val(n) {
    if n < 0 {
      return 0 - n
    }
    return n
  }

  dev clamp_val(val, lo, hi) {
    if val < lo {
      return lo
    }
    if val > hi {
      return hi
    }
    return val
  }

  dev draw_line(x0, y0, x1, y1, ch) {
    #dx = abs_val(x1 - x0)
    #dy = abs_val(y1 - y0)
    #sx = 1
    if x0 > x1 {
      #sx = 0 - 1
    }
    #sy = 1
    if y0 > y1 {
      #sy = 0 - 1
    }
    #err = dx - dy
    #cx = x0
    #cy = y0
    loop {
      pr(cx + "," + cy + ":" + ch)
      if cx == x1 {
        if cy == y1 {
          break
        }
      }
      #e2 = 2 * err
      if e2 > 0 - dy {
        #err = err - dy
        #cx = cx + sx
      }
      if e2 < dx {
        #err = err + dx
        #cy = cy + sy
      }
    }
    return 0
  }

  dev draw_rect(x, y, w, h, ch) {
    #i = 0
    while i < h {
      #j = 0
      while j < w {
        pr((x + j) + "," + (y + i) + ":" + ch)
        #j = j + 1
      }
      #i = i + 1
    }
    return 0
  }

  pr("=== McBL# 2D Game Engine ===")
  pr("")
  pr("Drawing rectangle at (10,5) size 20x8:")
  draw_rect(10, 5, 20, 8, "#")
  pr("")
  pr("Drawing diagonal line (0,0) to (79,24):")
  draw_line(0, 0, 79, 24, "*")
  pr("")
  pr("Drawing diagonal line (79,0) to (0,24):")
  draw_line(79, 0, 0, 24, "*")
  pr("")
  pr("=== Game Engine Demo Complete ===")
  CLEAR_cargo
endinc;
