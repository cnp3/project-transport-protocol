# Evaluating the transport protocol project

This folder contains our material we use to grade student's projects.

## Our grading principles

Due to the high number of students in the computer networking course, we have to rely on *automated grading*. Therefore, we enforce a project format with required Makefile commands as described in the project statement. In particular, the `make` command must support the two following targets.

```bash
# The following command generates two executables, 'sender' and 'receiver'
$ make
# The following command runs the tests provided by the project
$ make tests
```

If a project succeeds in generating the `sender` and `receiver` executables, it is considered for evaluation.

The grading is determined by running transfers under different conditions and checking that the file created by the `receiver` is the same as the one sent by the `sender`. The tests' outcome is binary: either it passes or it fails. Tests have two classifications: the *interoperability* and the *link characterization*.

From the interoperability point of view, there are two possible cases. *Self* tests consists in assessing that the `sender` and `receiver` of a given student's project can work together. Both sides must be functional to pass such tests. *Reference* tests observe if the provided `sender` binary (resp. `receiver`) interoperates with a reference `receiver` (resp. `sender`). It ensures that the student's implementation follows the protocol specifications.

In our tests, the link is either *perfect* or *imperfect*.
In the perfect case, there is no delay, packet corruption, truncation, loss or reordering. In the imperfect one, tests first evaluate a link only showing delay, corruption,... alone. A dedicated test evaluates the worst-case with all possible imperfections together.

## How to use our evaluation script

The `evaluate.py` script orchestrates the projects' grading on the target machine, but requires a few pre-processing as described below.

* You need to compile the reference implementation (e.g., **TODO**) and set the variables `REF_SENDER` and `REF_RECEIVER` to the absolute path of the `sender` and `receiver` binaries, respectively.
* For imperfect link tests, you need to compile the link simulator provided at **TODO** and set the variable `LINKSIM` to the absolute path of the `link_sim` executable.
* It may happen that student's code crashes or shows some non-determinism. The script stores the logs (stderr) of the binaries in the path pointer by the `BDIR` variable. Expect to have a lot of free memory.
* The script takes a directory as first parameter. The directory must contain one folder per student project, each of them following the expected format. An example of the expected format is shown below.
```
student_projects_dir
├── first_project
│   ├── Makefile
│   ├── src
│   │   ├── ...
│   └── tests
│       └── ...
├── second_project
│   ├── Makefile
│   ├── src
│   │   └── ...
│   └── tests
│       └── ...
...
```

Once the listed points are met, you can run the script with
```bash
python evaluate.py student_projects_dir
```
This will generate a JSON file containing the results of all the tests performed on all the student's projects contained in `student_projects_dir`. In case you have to stop the script to run it again later, you can restart the script with the `-R` flag and passing the current state of the JSON, for instance
```bash
python evaluate.py student_projects_dir -R old_json.json
```

## Credits

The main contributor to the evaluation script is Olivier Tilmans ([@oliviertilmans](https://github.com/oliviertilmans)). Quentin De Coninck ([@qdeconinck](https://github.com/qdeconinck)) added some test cases for packet truncation.