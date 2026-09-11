module arythmetics
  use iso_c_binding
  implicit none
contains
  

 subroutine process_file_from_c(input_c_str) bind(c, name ="process_file_from_c")
   character(kind=c_char), intent(in) :: input_c_str(*)   
   
   integer(c_int) :: file_unit
   integer :: io_status
   character(len=2) :: cmd
   real(kind=8) :: a, b , res
   character(len=255) :: f_filename
   integer :: i
   
   file_unit = 15

   f_filename = ""
   i = 1

   do while(input_c_str(i) /= c_null_char .and. i <= 255)
      f_filename(i:i) = input_c_str(i)
      i = i + 1
  end do
 
   open(unit=file_unit, file=trim(f_filename), status='old', action='read', iostat=io_status)
   
   if(io_status /= 0) then !ÐµÑÐ»Ð¸ Ñ„Ð°Ð¹Ð» Ð½Ðµ Ð·Ð°Ð¼ÐµÑ‡ÐµÐ½ Ð¸Ð»Ð¸ ÐºÐ°ÐºÐ¸ÐÐµÑ Ñ‚Ð¾ Ð¾Ð±ÑÑ‚Ð¾ÑÑ‚ÐµÐ»ÑŒÑÑ‚Ð²Ð°
      print*,"[FORTRAN ERROR]:could not open file. Or it doesn't exist, or i don't know"
      return
   end if

   do
      read(file_unit, *, iostat=io_status) cmd, a, b
      if(io_status < 0) exit
      if(io_status > 0) cycle
      
      select case(cmd)
      case ("T1")
          res = a + b
          print*,"the result is", res
      case ("T2")
          res = a - b
          print*,"the result is", res
      case ("T3")
          res = a * b
          print*,"the result is", res
      case ("T4")
          if(b /= 0.0d0) then
             res = a / b
             print*,"the result is", res 
          else
             print*,"[FORTRAN ERROR]: div by zero!"
          end if
      end select
    end do
    
    close(file_unit)
  end subroutine process_file_from_c
end module
