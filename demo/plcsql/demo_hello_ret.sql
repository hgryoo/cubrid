create or replace function demo_hello_ret() return varchar as 
begin
    return 'hello cubrid';
end;


create or replace function test_fc(a int) return int as 
begin
    return a;
end;


create or replace function test_fc2(a string) return varchar as 
begin
    return a;
end;
