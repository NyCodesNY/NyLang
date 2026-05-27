function void Main() {
    int x = 42;
    int* ptr = &x;
    int y = 200;
    
    System.Print("Original x:");
    System.Print(x);
    
    System.Print("Pointer read:");
    System.Print(*ptr);
    
    *ptr = 100;
    
    System.Print("Modified x:");
    System.Print(x);
    
    System.Print("Pointer arithmetic read (should be 200):");
    int* p_y = ptr - 2;
    System.Print(*p_y);
    
    *p_y = 500;
    System.Print("Modified y via arithmetic:");
    System.Print(y);
}
