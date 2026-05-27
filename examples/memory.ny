function void Main() {
    System.Print("Allocating memory...");
    int* ptr = System.Alloc(8);
    
    *ptr = 12345;
    System.Print("Value in allocated memory:");
    System.Print(*ptr);
    
    System.Print("Freeing memory...");
    System.Free(ptr);
    
    System.Print("Done!");
}
