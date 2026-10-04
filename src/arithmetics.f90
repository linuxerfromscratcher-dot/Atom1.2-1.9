
module arithmetics
  use iso_c_binding
  implicit none

  type, bind(C) :: atom_pair
    integer(c_int64_t) :: lo
    integer(c_int64_t) :: hi
  end type atom_pair

  type, bind(C) :: atom_cell
    integer(c_int64_t) :: words(4)
  end type atom_cell

  type, bind(C) :: atom_buffer
    type(c_ptr) :: data
    integer(c_int64_t) :: len
  end type atom_buffer

contains


  function atom_add(a, b) bind(c, name="atom_add") result(res)
    integer(c_int64_t), value, intent(in) :: a, b
    integer(c_int64_t) :: res
    res = a + b
  end function atom_add

  function atom_sub(a, b) bind(c, name="atom_sub") result(res)
    integer(c_int64_t), value, intent(in) :: a, b
    integer(c_int64_t) :: res
    res = a - b
  end function atom_sub

  function atom_mul(a, b) bind(c, name="atom_mul") result(res)
    integer(c_int64_t), value, intent(in) :: a, b
    integer(c_int64_t) :: res
    res = a * b
  end function atom_mul

  function atom_div(a, b) bind(c, name="atom_div") result(res)
    integer(c_int64_t), value, intent(in) :: a, b
    integer(c_int64_t) :: res
    if (b == 0_c_int64_t) then
      res = 0_c_int64_t
    else
      res = a / b
    end if
  end function atom_div

  function atom_abs(a) bind(c, name="atom_abs") result(res)
    integer(c_int64_t), value, intent(in) :: a
    integer(c_int64_t) :: res
    if (a < 0_c_int64_t) then
      res = -a
    else
      res = a
    end if
  end function atom_abs


  function atom_abi_word_size() bind(c, name="atom_abi_word_size") result(res)
    integer(c_int64_t) :: res
    res = int(c_sizeof(1_c_int64_t), c_int64_t)
  end function atom_abi_word_size

  function atom_abi_pair_size() bind(c, name="atom_abi_pair_size") result(res)
    integer(c_int64_t) :: res
    res = 2_c_int64_t * int(c_sizeof(1_c_int64_t), c_int64_t)
  end function atom_abi_pair_size

  function atom_abi_buffer_size() bind(c, name="atom_abi_buffer_size") result(res)
    integer(c_int64_t) :: res
    res = int(c_sizeof(c_null_ptr), c_int64_t) + int(c_sizeof(1_c_int64_t), c_int64_t)
  end function atom_abi_buffer_size

  subroutine atom_abi_fill_pair(buf) bind(c, name="atom_abi_fill_pair")
    type(atom_pair), intent(out) :: buf
    buf%lo = 1111111111_c_int64_t
    buf%hi = 2222222222_c_int64_t
  end subroutine atom_abi_fill_pair

  subroutine atom_abi_fill_buffer(buf) bind(c, name="atom_abi_fill_buffer")
    type(atom_buffer), intent(out) :: buf
    buf%data = c_null_ptr
    buf%len = 4242_c_int64_t
  end subroutine atom_abi_fill_buffer

  subroutine atom_abi_fill_cells(buf, n) bind(c, name="atom_abi_fill_cells")
    integer(c_int32_t), value, intent(in) :: n
    type(atom_cell), intent(out) :: buf(n)
    integer(c_int64_t) :: local(n, n)
    integer :: i, j

    if (n < 1) return

    do j = 1, n
      do i = 1, n
        local(i, j) = int(i - 1, c_int64_t) * 1000_c_int64_t + int(j - 1, c_int64_t)
      end do
    end do

    do j = 1, n
      do i = 1, n
        buf(j)%words(i) = local(i, j)
      end do
    end do
  end subroutine atom_abi_fill_cells

end module arithmetics
