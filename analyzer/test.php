<?php

$x = 'foo';
$y = $x;

class Bar {}

function dope() {
	static $evil;

	$evil = new Bar();
	
	return $evil;
}

dope();

class Foo {
	function asdf () {
		static $two;
		
		$two = 'two';
		
		return $two;
	}
}

$fooObj = new Foo();
$fooObj->asdf();

gc_collect_cycles();
meminfo_dump(fopen('/Volumes/repos/GoREACT/GoREACT/.cachegrind/memdumpasdf.json', 'w'));
var_dump(dump_static_vars());


