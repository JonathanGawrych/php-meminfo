--TEST--
Check that error handlers are accounted for
--SKIPIF--
<?php
    if (!extension_loaded('json')) die('skip json ext not loaded');
?>
--FILE--
<?php
    $dump = fopen('php://memory', 'rw');

    Class MyHandler {
        function __construct($which) {
            $this->$which = true;
        }
        function __invoke() {}
    }

    // set error handlers that'll be pushed onto the bottom of the stack
    set_error_handler(new MyHandler('MyPrevousPreviousError'));
    set_exception_handler(new MyHandler('MyPreviousPreviousException'));

    // set error handlers that'll be pushed onto the top of the stack
    set_error_handler(new MyHandler('MyPrevousError'));
    set_exception_handler(new MyHandler('MyPreviousException'));

    // set the current error handlers
    set_error_handler(new MyHandler('MyError'));
    set_exception_handler(new MyHandler('MyException'));

    meminfo_dump($dump);

    rewind($dump);
    $meminfoData = json_decode(stream_get_contents($dump), true);
    fclose($dump);

    $myArrayDump = [];

    $frames = ['<ERROR_HANDLER>','<EXCEPTION_HANDLER>','<PREVIOUS_ERROR_HANDLER>','<PREVIOUS_EXCEPTION_HANDLER>'];
    foreach ($meminfoData['items'] as $item) {
        if (isset($item['frame']) && in_array($item['frame'], $frames)) {
            echo "Frame: " . $item['frame'] . "\n";
            echo "  Type: " . $item['type'] . "\n";
            echo "  Class: " . $item['class'] . "\n";
            echo "  Is root: " . $item['is_root'] . "\n";
            echo "  Children: \n";
            if (isset($item['children'])) {
                foreach($item['children'] as $symbol => $address) {
                    echo "    $symbol\n";
                }
            }
        }
    }
?>
--EXPECT--
Frame: <ERROR_HANDLER>
  Type: object
  Class: MyHandler
  Is root: 1
  Children: 
    MyError
Frame: <EXCEPTION_HANDLER>
  Type: object
  Class: MyHandler
  Is root: 1
  Children: 
    MyException
Frame: <PREVIOUS_ERROR_HANDLER>
  Type: object
  Class: MyHandler
  Is root: 1
  Children: 
    MyPrevousError
Frame: <PREVIOUS_ERROR_HANDLER>
  Type: object
  Class: MyHandler
  Is root: 1
  Children: 
    MyPrevousPreviousError
Frame: <PREVIOUS_EXCEPTION_HANDLER>
  Type: object
  Class: MyHandler
  Is root: 1
  Children: 
    MyPreviousException
Frame: <PREVIOUS_EXCEPTION_HANDLER>
  Type: object
  Class: MyHandler
  Is root: 1
  Children: 
    MyPreviousPreviousException
