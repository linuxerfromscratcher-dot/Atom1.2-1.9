module arithmetics
  use iso_c_binding
  implicit none
contains

  function atom_add(a, b) bind(c, name="atom_add") result(res)
    integer(c_long), value, intent(in) :: a, b
    integer(c_long) :: res
    res = a + b
  end function atom_add

  function atom_sub(a, b) bind(c, name="atom_sub") result(res)
    integer(c_long), value, intent(in) :: a, b
    integer(c_long) :: res
    res = a - b
  end function atom_sub

  function atom_mul(a, b) bind(c, name="atom_mul") result(res)
    integer(c_long), value, intent(in) :: a, b
    integer(c_long) :: res
    res = a * b
  end function atom_mul

  function atom_div(a, b) bind(c, name="atom_div") result(res)
    integer(c_long), value, intent(in) :: a, b
    integer(c_long) :: res
    if (b == 0_c_long) then
      res = 0_c_long
    else
      res = a / b
    end if
  end function atom_div

  function atom_abs(a) bind(c, name="atom_abs") result(res)
    integer(c_long), value, intent(in) :: a
    integer(c_long) :: res
    if (a < 0_c_long) then
      res = -a
    else
      res = a
    end if
  end function atom_abs

end module arithmetics
